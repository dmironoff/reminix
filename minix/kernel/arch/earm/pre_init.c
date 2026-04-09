__asm__(".arch armv7-a\n\t.arch_extension virt");

#define UNPAGED 1	/* for proper kmain() prototype */


#include "arch_configs.h"
#include "kernel/kernel.h"
#include <assert.h>
#include <stdlib.h>
#include <minix/minlib.h>
#include <minix/const.h>
#include <minix/type.h>
#include <minix/board.h>
#include <minix/com.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/reboot.h>
#include "string.h"
#include "arch_proto.h"
#include "direct_utils.h"
#include "bsp_serial.h"
#include "glo.h"
#include <machine/multiboot.h>
#include "modules_memory_map.h"
#include <libfdt.h>
#include <minix/physmemorymap.h>
#include <minix/abstract_pagetables.h>
#include "kernel/bootstrap_kernel_information.h"
#include "bsp_devices_mmap.h"
#include "kernel/mmap_utils.h"
#include "bsp_smp_info.h"
#include "pagetables.h"
#include "kernel/apt_utils.h"
#include "kernel/env_params_utils.h"
#include "kernel/bootargs_utils.h"



#if USE_SYSDEBUG
#define MULTIBOOT_VERBOSE 1
#endif

struct kmessages kmessages;

/* pg_utils.c uses this; in this phase, there is a 1:1 mapping. */
phys_bytes vir2phys(void *addr) { return (phys_bytes) addr; }


/* String length used for mb_itoa */
#define ITOA_BUFFER_SIZE 20

/* kernel bss */
extern u32_t _edata;
extern u32_t _end;

/* kernel unpaged bss */
extern char _kern_unpaged_edata;
extern char _kern_unpaged_end;


/*Наша замечательная структура с параметрами для kmain*/
bootstrap_kernel_information_t bki;
/*Таблица памяти */
mmap_t bootstrap_mmap;
/* Регионы в этой таблице, мы при старте сначала инициализируем их предопределённое значение
 *  А уже после kmain эта структура станет динамической
 */
mmap_region_t bootstrap_mmap_regions[BOOTSTRAP_MMAP_REGIONS];
/*Абстрактная таблица страниц ядра*/
vm_abstract_pt_t bootstrap_kernel_apt;
vm_abstract_pt_l1_entry_t bootstrap_kernel_apt_l1_entries[ARM_KERNEL_L1_PAGES];

/*Абстрактные таблицы страниц для процессов*/
vm_abstract_pagetables_t bootstrap_abstract_pagetables;
vm_abstract_pt_t bootstrap_apt[BOOTSTRAP_APT_COUNT];
vm_abstract_pt_l1_entry_t bootstrap_apt_enties[BOOTSTRAP_APT_COUNT * ARM_USER_L1_PAGES];


/* 
 * During low level init many things are not supposed to work
 * serial being one of them. We therefore can't rely on the
 * serial to debug. POORMANS_FAILURE_NOTIFICATION can be used
 * before we setup our own vector table and will result in calling
 * the bootloader's debugging methods that will hopefully show some
 * information like the currnet PC at on the serial.
 */
#define POORMANS_FAILURE_NOTIFICATION  asm volatile("svc #00\n")


/*
 * На бедную функцию pre_init выпала теперь тяжкая доля:
 * Разобрать fdt
 * Настроить процессор и MMU
 * Физически переместить рабочую(не bootstrap) часть ядра в начало оперативной памяти - ну мне так нравится
 *                                   А ещё это уменьшит количество непригодных дыр в памяти
 * Переместить fdt рядышком с ядром
 * Выделить память под общую карту памяти, абстрактные таблицы страниц, физические таблицы страниц
 * Разметить все модули в память ядра, для начала, что бы kmain и vm уже занялись их постепенным запуском
 * Включить MMU
 * Теперь мы в pre_init включаем уже не предварительный, а боевой режим
 * Тоесть kmain уже будет работать на чистой системе, на которой ему не потребуется донастраивать MMU
 *
 * и прыгнуть в kmain передав ему  bootstrap_kernel_information_t
 */
bootstrap_kernel_information_t *pre_init(int argc, char **argv)
{
    extern char _kern_phys_base, _kern_vir_base, _kern_size,
            _kern_unpaged_start, _kern_unpaged_end;
    u32_t len; // Для  общих нужд
    void *temp_pointer = 0; // Тоже для общих нужд итерации и связанных списков
    int node; // Для работы libfdt
    int module_id = 0;
    mmap_region_t free_region; // При разметке сюда будем класть данные о свободной памяти

	/* This is the main "c" entry point into the kernel. It gets called
	   from head.S */

    /* Так мы теперь используем протокол загрузки linux из u-boot
     * и поэтому сохраним адрес fdt в отдельную переменную
     * */
    if (bki.fdt_addr == 0) {
        asm volatile ("mov %0, r2" : "=r"(bki.fdt_addr));
    }

    /*
     *  На всякий случай поменяем режим работы процессора,
     *  А то у u-boot при загрузке через протокол linux какой-то зоопарк
     *  в котором я не разобрался, но может это я просто тупой. Хуй с ним
     *  просто переключим режим если он не SVC
     */
    u32_t cpsr = read_cpsr(); // Младшие 5 бит регистра это mode
    if ((cpsr & 0x1f) == ARM_CPU_MODE_HYP) {
        // Если мы какого-то хуя в режиме гипервизора,
        // то нужна специальная конструкция что бы прыгнуть в SVC
        // Нет, мне нравится режим гипервизора, просто пока ReMinix не знает что делать с таким богадством
        cpsr &= (~0x1f);
        cpsr |= ARM_CPU_MODE_SVC;
        asm volatile ("msr spsr_hyp, %0" : : "r"(cpsr));
        asm volatile ("msr elr_hyp, %0" : : "r"(pre_init));
        asm volatile ("eret");
    } else if ((cpsr & 0x1f) != ARM_CPU_MODE_SVC) {
        // Если нас каким-то штормом пронесло мимо SVC, то это наша последняя надежда уйти туда
        cpsr &= (~0x1f);
        cpsr |= ARM_CPU_MODE_SVC;
        asm volatile ("msr cpsr_c, %0" : : "r"(cpsr));
    }

    disable_mmu(); // Отключаем MMU и кеши, на случай если uboot его включил
    // Если MMU уже или всё ещё отключён, то ничего не случится, но u-boot иногда любит оставлять его включённым
    dcache_clean(); /* Очищаем все кеши, просто вталкиваем в оперативку что закешировал процессор */

	/* Clear BSS */
	memset(&_edata, 0, (u32_t)&_end - (u32_t)&_edata);
    memset(&_kern_unpaged_edata, 0, (u32_t)&_kern_unpaged_end - (u32_t)&_kern_unpaged_edata);

    bsp_ser_init();
    bsp_ser_putc('1');

    // Проверка магического числа fdt
    if (!fdt_check_header((void *) bki.fdt_addr)) {
        bsp_ser_putc('\n');
        bsp_ser_putc('1');
        while(1);
    }

    bootstrap_mmap.first_region = bootstrap_mmap_regions;
    bootstrap_mmap.regions = bootstrap_mmap_regions;
    bootstrap_mmap.regions_allocated = BOOTSTRAP_MMAP_REGIONS;
    bootstrap_mmap.regions_count = 0;
    bootstrap_mmap.version = 0;
    bootstrap_mmap.last_region = bootstrap_mmap_regions;
    bootstrap_mmap.need_defragmentation = 0;

    // Сначала мы разметим известные нам регионы с регистрами устройств переферии
    bsp_devices_mmap_t *devices_mmap;
    bsp_devices_mmap(devices_mmap, &len);
    for (int i = 0; i < len; i++) {
        bootstrap_mmap.regions[i].type = MMAP_DEVICE;
        bootstrap_mmap.regions[i].start = devices_mmap[i].start;
        bootstrap_mmap.regions[i].size = devices_mmap[i].size;
        bootstrap_mmap.regions[i].cache_hint = MMAP_CACHE_NO;
        bootstrap_mmap.regions[i].refcount = 0;
        bootstrap_mmap.regions[i].flags = 0;
        bootstrap_mmap.regions[i].next_region = 0;
        if (i == 0) {
            bootstrap_mmap.regions[i].prev_region = 0;
        } else {
            bootstrap_mmap.regions[i].prev_region = bootstrap_mmap.last_region;
            bootstrap_mmap.last_region->next_region = &bootstrap_mmap.regions[i];
            bootstrap_mmap.last_region = &bootstrap_mmap.regions[i];
        }
        bootstrap_mmap.regions_count++;
    }
    bootstrap_mmap.version++;

    bsp_ser_putc('2');

    // Теперь нам нужно узнать из FDT размер оперативной памяти и регионы этой памяти
    // #TODO Может быть несколько узлов /memory@...
    node = fdt_path_offset((void *)bki.fdt_addr, "/memory");
    if (node < 0) {
        node = fdt_node_offset_by_prop_value((void *)bki.fdt_addr, -1, "device_type", "memory", 7);
    }

    if (node >= 0) {
        const u32_t *reg;
        reg = fdt_getprop((void *)bki.fdt_addr, node, "reg", &len);
        if (reg != NULL && len > 0) {
            len = len / 4 ; // fdt_getprop возвращает длинну в байтах
            for (int i = 0; i < len; i += 2) {
                bootstrap_mmap.regions[bootstrap_mmap.regions_count]
                        .start = fdt32_to_cpu(reg[0]);
                bootstrap_mmap.regions[bootstrap_mmap.regions_count]
                        .size = fdt32_to_cpu(reg[1]);
                bki.mem_start = fdt32_to_cpu(reg[0]);
                bki.mem_end = fdt32_to_cpu(reg[1]);
                bootstrap_mmap.total_mem += bootstrap_mmap.regions[bootstrap_mmap.regions_count].size;
                bootstrap_mmap.free_mem += bootstrap_mmap.regions[bootstrap_mmap.regions_count].size;
                bootstrap_mmap.regions[bootstrap_mmap.regions_count]
                        .type = MMAP_FREE;
                bootstrap_mmap.regions[bootstrap_mmap.regions_count]
                        .cache_hint = MMAP_CACHE_NORMAL;

                bootstrap_mmap.regions[bootstrap_mmap.regions_count]
                        .prev_region = bootstrap_mmap.last_region;
                bootstrap_mmap.last_region->next_region = &bootstrap_mmap.regions[bootstrap_mmap.regions_count];
                bootstrap_mmap.last_region = &bootstrap_mmap.regions[bootstrap_mmap.regions_count];
                bootstrap_mmap.regions_count++;
            }
            bootstrap_mmap.version++;
        } else {
            while(1);
        }
    } else {
        while(1);
    }

    // Теперь мы хоть вкурсах за размер оперативной памяти на доске
    // И подготовили чистую карту памяти

    // Разберём командную строку из /chosen
    node = fdt_path_offset((void *)bki.fdt_addr, "/chosen");
    if (node >= 0) {
        const char *bootargs = fdt_getprop((void *)bki.fdt_addr, node, "bootargs", &len);
        if (bootargs) {
            bootargs_to_params(bootargs, bki.params);
        }
    }

    // Пришло время внести в нашу карту памяти модули и ядро

    // Начнём с модулей, вернее с загруженных образов
    // Мы сразу внесём их в карту памяти, что бы случайно не затереть их
    node = fdt_path_offset((void *)bki.fdt_addr, "/miniximages");
    if (node >= 0) {
        int child_offset;
        for (child_offset = fdt_first_subnode((void *)bki.fdt_addr, node);
            child_offset >= 0;
            child_offset = fdt_next_subnode((void *)bki.fdt_addr, child_offset)) {

            const char *nodename = fdt_get_name((void *)bki.fdt_addr, child_offset, &len);
            if (!nodename) {
                continue;
            }
            const char *type = fdt_getprop((void *)bki.fdt_addr, child_offset, "type", &len);
            if (!type) {
                continue;
            }
            const uint32_t *addr = fdt_getprop((void *)bki.fdt_addr, child_offset, "addr", &len);
            if (!addr) {
                continue;
            }
            const uint32_t *size = fdt_getprop((void *)bki.fdt_addr, child_offset, "size", &len);
            if (!size) {
                continue;
            }

            mmap_region_t new_region;
            new_region.size = fdt32_to_cpu((fdt32_t)*size);
            new_region.start = fdt32_to_cpu((fdt32_t)*addr);
            new_region.type = MMAP_BOOT_MOD;
            mmap_add_region(&bootstrap_mmap, &new_region);

            bki.modules[module_id].addr = fdt32_to_cpu((fdt32_t)*addr);
            bki.modules[module_id].size = fdt32_to_cpu((fdt32_t)*size);
            strcpy(bki.modules[module_id].name, nodename);
            if (strcmp(type, "server") == 0) {
                bki.modules[module_id].type = BOOT_MODULE_SERVER;
            } else if (strcmp(type, "service") == 0) {
                bki.modules[module_id].type = BOOT_MODULE_SERVICE;
            } else if (strcmp(type, "filesystem") == 0) {
                bki.modules[module_id].type = BOOT_MODULE_FS;
            } else if (strcmp(type, "driver") == 0) {
                bki.modules[module_id].type = BOOT_MODULE_DRIVER;
            } else if (strcmp(type, "init") == 0) {
                bki.modules[module_id].type = BOOT_MODULE_INIT;
            } else if (strcmp(type, "user") == 0) {
                bki.modules[module_id].type = BOOT_MODULE_USERPROC;
            } else if (strcmp(type, "config") == 0) {
                bki.modules[module_id].type = BOOT_MODULE_CONFIG;
            } else {
                bki.modules[module_id].type = BOOT_MODULE_UNKNOWN;
            }

            module_id++;
        }
    } else {
        while(1);
    }

    // Разметим на карте текущее местонахождение FDT и Ядра, что бы успешно не перекрывая ничего их переместить
    mmap_region_t fdt_region;
    fdt_region.size = fdt_totalsize((void *) bki.fdt_addr);
    fdt_region.start = (phys_bytes) bki.fdt_addr;
    fdt_region.type = MMAP_BOOT_MOD;
    mmap_add_region(&bootstrap_mmap, &fdt_region);
    /* После перемещения добавим его в инфу
    module_id++;
    bki.modules[module_id].addr = fdt_totalsize((void *) bki.fdt_addr);
    bki.modules[module_id].size = (phys_bytes) bki.fdt_addr;
    bki.modules[module_id].name = "fdt";
    bki.modules[module_id].type = BOOT_MODULE_FDT;
    */

    mmap_region_t kernel_region;
    kernel_region.start = (phys_bytes) &_kern_phys_base;
    kernel_region.size = (phys_bytes) _kern_size;
    kernel_region.type = MMAP_KERNEL;
    mmap_add_region(&bootstrap_mmap, &kernel_region);

    // Ну чё? Поехали перетаскивать всё это и выделять память под наши структуры данных
    mmap_region_t new_kernel_location;
    if (mmap_find_lowest_free_region(&bootstrap_mmap, (phys_bytes) _kern_size, &new_kernel_location)) {
        memcpy((void *)new_kernel_location.start, (void *)kernel_region.start, (uint32_t) _kern_size);
        mmap_add_region(&bootstrap_mmap, &new_kernel_location);
    } else {
        while(1);
    }

    // Новая локация FDT
    mmap_region_t new_fdt_location;
    if (mmap_find_lowest_free_region(&bootstrap_mmap, fdt_region.size, &new_fdt_location)) {
        memcpy((void *)new_fdt_location.start, (void *)fdt_region.start, (uint32_t)fdt_region.size);
        mmap_add_region(&bootstrap_mmap, &new_fdt_location);
    } else {
        while(1);
    }

    // абстрактные таблицы памяти
    mmap_region_t new_apt_location;
    mmap_region_t new_apt_l1_entries;
    mmap_region_t new_apt_l2_entries;
    mmap_region_t new_apt_tables;

    vm_abstract_pagetables_t *new_apt;
    if (mmap_find_lowest_free_region(&bootstrap_mmap, sizeof (vm_abstract_pagetables_t), &new_apt_location)) {
        new_apt = (vm_abstract_pagetables_t *) new_apt_location.start;
        new_apt_location.type = MMAP_KRNL_APT;
        mmap_add_region(&bootstrap_mmap, &new_apt_location);

        mmap_find_lowest_free_region(&bootstrap_mmap, sizeof (vm_abstract_pt_t) * BOOTSTRAP_APT_COUNT + 3, &new_apt_tables);
        new_apt_tables.type = MMAP_KRNL_APT;
        mmap_add_region(&bootstrap_mmap, &new_apt_tables);
        // new_apt->tables = (vm_abstract_pt_t *) new_apt_tables.start; забыл что тут нужен виртуальный адрес
        new_apt->pagetables_allocated = new_apt_tables.size / sizeof(vm_abstract_pt_t) - 3;

        mmap_find_lowest_free_region(&bootstrap_mmap, sizeof (vm_abstract_pt_l1_entry_t ) * BOOTSTRAP_APT_L1_COUNT + 3, &new_apt_l1_entries);
        new_apt_l1_entries.type = MMAP_KRNL_APT;
        mmap_add_region(&bootstrap_mmap, &new_apt_l1_entries);

        mmap_find_lowest_free_region(&bootstrap_mmap, sizeof (vm_abstract_pt_l2_entry_t ) * BOOTSTRAP_APT_L2_COUNT + 3, &new_apt_l2_entries);
        new_apt_l2_entries.type = MMAP_KRNL_APT;
        mmap_add_region(&bootstrap_mmap, &new_apt_l2_entries);


    } else {
        while(1);
    }

    // Физические страницы памяти
    mmap_region_t new_pt_location;
    mmap_region_t new_pt_l1_location;
    mmap_region_t new_pt_l2_location;
    mmap_find_lowest_free_region(&bootstrap_mmap, sizeof (arm_pt_t) * ARM_MAX_PT_HANDLES, &new_pt_location);
    new_pt_location.size = sizeof (arm_pt_t) * ARM_MAX_PT_HANDLES;
    new_pt_location.type = MMAP_PAGETABLES;
    new_pt_location.cache_hint = MMAP_CACHE_NORMAL;
    mmap_add_region(&bootstrap_mmap, &new_pt_location);

    mmap_find_lowest_free_aligned_region(&bootstrap_mmap, sizeof (uint32_t) * ARM_MAX_PT_HANDLES * ARM_USER_L1_PAGES,
                                         1024*1024, &new_pt_l1_location);
    new_pt_l1_location.start = (new_pt_l1_location.start + 1024 * 1024) & ~(1024 * 1024);
    new_pt_l1_location.size = sizeof (uint32_t) * ARM_MAX_PT_HANDLES * ARM_USER_L1_PAGES;
    new_pt_l1_location.type = MMAP_PAGETABLES;
    new_pt_l1_location.cache_hint = MMAP_CACHE_NORMAL;
    mmap_add_region(&bootstrap_mmap, &new_pt_l1_location);


    mmap_find_lowest_free_aligned_region(&bootstrap_mmap, sizeof (uint32_t) * ARM_MAX_PT_HANDLES * ARM_USER_L1_PAGES * 256,
                                         1024, &new_pt_l2_location);
    new_pt_l2_location.start = (new_pt_l2_location.start + 1024) & ~(1024);
    new_pt_l2_location.size = sizeof (uint32_t) * ARM_MAX_PT_HANDLES * ARM_USER_L1_PAGES * 256;
    new_pt_l2_location.type = MMAP_PAGETABLES;
    new_pt_l2_location.cache_hint = MMAP_CACHE_NORMAL;
    mmap_add_region(&bootstrap_mmap, &new_pt_l2_location);


    mmap_region_t kernel_pt_location;
    mmap_region_t kernel_pt_l1_location;
    mmap_region_t kernel_pt_l2_location;

    mmap_find_lowest_free_region(&bootstrap_mmap, sizeof (arm_pt_t), &kernel_pt_location);
    kernel_pt_location.size = sizeof (arm_pt_t);
    kernel_pt_location.type = MMAP_PAGETABLES;
    kernel_pt_location.cache_hint = MMAP_CACHE_NORMAL;
    mmap_add_region(&bootstrap_mmap, &kernel_pt_location);

    mmap_find_lowest_free_aligned_region(&bootstrap_mmap, sizeof (uint32_t) * ARM_KERNEL_L1_PAGES,
                                         2048, &kernel_pt_l1_location);
    kernel_pt_l1_location.start = (kernel_pt_l1_location.start + 2048) & ~(2048);
    kernel_pt_l1_location.size = sizeof (uint32_t) * ARM_KERNEL_L1_PAGES;
    kernel_pt_l1_location.type = MMAP_PAGETABLES;
    kernel_pt_l1_location.cache_hint = MMAP_CACHE_NORMAL;
    mmap_add_region(&bootstrap_mmap, &kernel_pt_l1_location);

    mmap_find_lowest_free_aligned_region(&bootstrap_mmap, sizeof (uint32_t) *  ARM_KERNEL_L1_PAGES * 256,
                                         1024, &kernel_pt_l2_location);
    kernel_pt_l2_location.start = (new_pt_l2_location.start + 1024) & ~(1024);
    kernel_pt_l2_location.size = sizeof (uint32_t) *  ARM_KERNEL_L1_PAGES * 256;
    kernel_pt_l2_location.type = MMAP_PAGETABLES;
    kernel_pt_l2_location.cache_hint = MMAP_CACHE_NORMAL;
    mmap_add_region(&bootstrap_mmap, &kernel_pt_l2_location);

    // Теперь нужно всему дать виртуальные адреса
    // Начнём с ядра, ох блять
    // Главная трудность в том что виртуальные адреса зависят от APT, a APT зависит от виртуальных адресов
    // Так что мы сначала разметим физические страницы всего подряд
    // Загрузим всё регистры, включим MMU
    arm_pt_t *kernel_pt = (arm_pt_t *) kernel_pt_location.start;
    kernel_pt->l1_phys = kernel_pt_l1_location.start;
    kernel_pt->l2_phys = kernel_pt_l2_location.start;
    vir_bytes kernel_virt_start = (vir_bytes) &_kern_vir_base;
#define MMU_KERNEL_FLAGS 0x1540E // Пока так оставим на ближайшее время
    map_mmap_region_to_kernel_pt_l1 ((uint32_t *) kernel_pt->l1_phys, &new_kernel_location, &kernel_virt_start, MMU_KERNEL_FLAGS);
    // Теперь в нагляк разметим область данных для таблиц страниц, напомню, что они у нас доступны только ядру
    // Таблицы страниц ядра будут по адресу 0xE0000000 (ARM_KERNEL_VIRT_START)
    vir_bytes kernel_virt_pt_start = ARM_KERNEL_VIRT_START;
    vir_bytes kernel_pt_next_block = map_mmap_region_to_kernel_pt_l1 ((uint32_t *) kernel_pt->l1_phys, &kernel_pt_location,
                                                                      &kernel_virt_pt_start, MMU_KERNEL_FLAGS);
    bki.kernel_pt = (arm_pt_t *) kernel_virt_pt_start;
    kernel_virt_pt_start = kernel_pt_next_block;
    kernel_pt_next_block = map_mmap_region_to_kernel_pt_l1 ((uint32_t *) kernel_pt->l1_phys, &kernel_pt_l1_location,
                                                                      &kernel_virt_pt_start, MMU_KERNEL_FLAGS);
    kernel_pt->l1_table = (uint32_t *) kernel_virt_pt_start;

    kernel_virt_pt_start = kernel_pt_next_block;
    kernel_pt_next_block = map_mmap_region_to_kernel_pt_l1 ((uint32_t *) kernel_pt->l1_phys, &kernel_pt_l2_location,
                                                                      &kernel_virt_pt_start, MMU_KERNEL_FLAGS);
    kernel_pt->l2_tables = (uint32_t *) kernel_virt_pt_start;
    // ЕБААТЬ! ЕБААТЬ! Это наши первые виртуальные указатели

    // Разметим новую структуру для BKI
    mmap_find_lowest_free_region(&bootstrap_mmap, sizeof(bootstrap_kernel_information_t), &free_region);
    mmap_region_t new_bki_location;
    new_bki_location.start = free_region.start;
    new_bki_location.size = sizeof(bootstrap_kernel_information_t);
    new_bki_location.type = MMAP_BKI;
    mmap_add_region(&bootstrap_mmap, &new_bki_location);
    bootstrap_kernel_information_t *new_bki = (bootstrap_kernel_information_t *) new_bki_location.start;
    vir_bytes new_bki_vir_addr = kernel_pt_next_block;
    kernel_pt_next_block = map_mmap_region_to_kernel_pt_l1 ((uint32_t *) kernel_pt->l1_phys, &new_bki_location,
                                                            &new_bki_vir_addr, MMU_KERNEL_FLAGS);
    // Размечаем в ядро FDT
    new_bki->fdt_addr = kernel_pt_next_block;
    kernel_pt_next_block = map_mmap_region_to_kernel_pt_l1 ((uint32_t *) kernel_pt->l1_phys, &new_fdt_location,
                                                            &new_bki->fdt_addr, MMU_KERNEL_FLAGS);
    // Размечаем для ядра все таблицы страниц пользовательских процессов
    new_bki->user_pt_base = kernel_pt_next_block;
    kernel_pt_next_block = map_mmap_region_to_kernel_pt_l1 ((uint32_t *)  kernel_pt->l1_phys, &new_pt_location,
                                                            &new_bki->user_pt_base, MMU_KERNEL_FLAGS);
    arm_pt_t *user_pts = (arm_pt_t *) new_pt_location.start;

    vir_bytes user_pts_l1_vir_start = kernel_pt_next_block;
    kernel_pt_next_block = map_mmap_region_to_kernel_pt_l1 ((uint32_t *) kernel_pt->l1_phys, &new_pt_l1_location,
                                                            &user_pts_l1_vir_start, MMU_KERNEL_FLAGS);

    vir_bytes user_pts_l2_vir_start = kernel_pt_next_block;
    kernel_pt_next_block = map_mmap_region_to_kernel_pt_l1 ((uint32_t *)  kernel_pt->l1_phys, &new_pt_l2_location,
                                                            &kernel_virt_pt_start, MMU_KERNEL_FLAGS);

    // Не забываем поправить все указатели в таблицах пользовательских страниц
    for(int i = 0; i < ARM_MAX_PT_HANDLES; i++) {
        // sizeof (uint32_t) * ARM_MAX_PT_HANDLES * ARM_USER_L1_PAGES

        user_pts[i].l1_phys = new_pt_l1_location.start + (i * sizeof(uint32_t) * ARM_USER_L1_PAGES);
        user_pts[i].l2_phys = new_pt_l2_location.start + (i * sizeof(uint32_t) * ARM_USER_L1_PAGES * 256);

        user_pts[i].l1_table = (uint32_t *) (user_pts_l1_vir_start + (i * sizeof(uint32_t) * ARM_USER_L1_PAGES));
        user_pts[i].l2_tables = (uint32_t *) (user_pts_l2_vir_start + (i * sizeof(uint32_t) * ARM_USER_L1_PAGES * 256));
    }

    // Теперь разметим временную таблицу для ttbr0 с разметкой 1 к 1
    // Это позволит нам включить MMU что бы доинициализировать память
    // Перед прыжком мы включим уже рабочую адресацию со всеми размеченными объектами
    // Мы будем давать адреса для наших структур сконца таблицы, что бы они не мешались
    vir_bytes user_pt_end = 0x1FFFFFFF; // Конец памяти пользователя
    // APT
    user_pts[0].status = PT_USED;
    user_pts[0].proc_ep = 0;
    vir_bytes user_pt_next_block = map_mmap_region_to_pt_l1_from_end((uint32_t *) user_pts[0].l1_phys, &new_apt_location,
                                                            &user_pt_end, MMU_KERNEL_FLAGS);
    user_pt_end = user_pt_next_block;
    new_bki->abstract_pagetables = (vm_abstract_pagetables_t *) user_pt_end;
    user_pt_next_block = map_mmap_region_to_pt_l1_from_end ((uint32_t *) user_pts[0].l1_phys, &new_apt_l1_entries,
                                                          &user_pt_end, MMU_KERNEL_FLAGS);
    new_apt->l1_entries = (vm_abstract_pt_l1_entry_t *) user_pt_end;

    user_pt_end = user_pt_next_block;
    user_pt_next_block = map_mmap_region_to_pt_l1_from_end ((uint32_t *) user_pts[0].l1_phys, &new_apt_l2_entries,
                                                          &user_pt_end, MMU_KERNEL_FLAGS);
    new_apt->l2_entries = (vm_abstract_pt_l2_entry_t *) user_pt_end;

    user_pt_end = user_pt_next_block;
    user_pt_next_block = map_mmap_region_to_pt_l1_from_end ((uint32_t *) user_pts[0].l1_phys, &new_apt_tables,
                                                          &user_pt_end, MMU_KERNEL_FLAGS);
    new_apt->tables = (vm_abstract_pt_t *) user_pt_end;

    // Карта памяти новая, после включения MMU мы сюда перенесём текущую
    mmap_region_t new_mmap_location;
    mmap_region_t new_mmap_regions;
    mmap_t *newmmap;
    mmap_find_lowest_free_region(&bootstrap_mmap, sizeof (mmap_t), &new_mmap_location);
    new_mmap_location.type = MMAP_KRNL_MMAP;
    new_mmap_location.size = sizeof(mmap_t);
    mmap_add_region(&bootstrap_mmap, &new_mmap_location);
    newmmap = (mmap_t *) new_mmap_location.start;

    user_pt_end = user_pt_next_block;
    user_pt_next_block = map_mmap_region_to_pt_l1_from_end ((uint32_t *) user_pts[0].l1_phys, &new_mmap_location,
                                                          &user_pt_end, MMU_KERNEL_FLAGS);
    new_bki->mmap = (mmap_t *) user_pt_end;

    mmap_find_lowest_free_region(&bootstrap_mmap, sizeof(mmap_region_t) * 1024, &new_mmap_regions);
    new_mmap_regions.type = MMAP_KRNL_MMAP;
    new_mmap_regions.size = sizeof(mmap_region_t) * 1024;
    mmap_add_region(&bootstrap_mmap, &new_mmap_regions);
    user_pt_end = user_pt_next_block;
    user_pt_next_block = map_mmap_region_to_pt_l1_from_end ((uint32_t *) user_pts[0].l1_phys, &new_mmap_regions,
                                                            &user_pt_end, MMU_KERNEL_FLAGS);
    newmmap->regions = (mmap_region_t *) user_pt_end;

    // А теперь делаем адресацию 1 к 1 для остальных страниц

// Ну пока так, после отладки основного кода будем уже правильно выставлять флаги.
// Пока это рабочие флаги

#define MMU_DDR_FLAGS    0x1140E // Кэшируемая RAM
#define MMU_DEVICE_FLAGS 0x00406 // Регистры (UART, CCU, GIC)
    for (int i = 0; i < ARM_USER_L1_PAGES; i++) {
        uint32_t *l1_phys = (uint32_t *) user_pts[0].l1_phys;
        if (l1_phys[i] == 0) {
            mmap_region_t *maped_region;
            if (mmap_find_region_by_addr(&bootstrap_mmap, i * ARM_SECTION_SIZE, maped_region)) {
               if (maped_region->type == MMAP_DEVICE) {
                   // Устройство
                   l1_phys[i] =  ((i * ARM_SECTION_SIZE) & ARM_VM_SECTION_MASK ) | MMU_DEVICE_FLAGS;
               } else if (maped_region->type == MMAP_KERNEL) {
                   // Размаплена, на ядро
                   l1_phys[i] =  ((i * ARM_SECTION_SIZE) & ARM_VM_SECTION_MASK ) | MMU_KERNEL_FLAGS;
               } else {
                   // Размаплена на какую-то другую хуиту
                   l1_phys[i] =  ((i * ARM_SECTION_SIZE) & ARM_VM_SECTION_MASK ) | MMU_DDR_FLAGS;
               }
            } else {
                // Pagefault
                l1_phys[i] = 0;
            }
        }
    }

    // Между делом определим и запомним параметры многоядерности
    new_bki->system_cpu_count = bsp_smp_get_cpu_count();
    new_bki->boot_cpu_number = bsp_smp_get_current_cpu();

    // ВСЁ. Включаем MMU
    dcache_clean();
    pg_load_ttbr1(bki.kernel_pt);
    pg_load_ttbr0(&user_pts[0]);
    vm_enable_paging();

    // Ну что пришло время всё привести в порядок и отдать управление основной части ядра
    new_bki = (bootstrap_kernel_information_t *) new_bki_vir_addr;
    new_bki->kernel_pt_handler = 0;

    memcpy(new_bki->modules, bki.modules, sizeof(boot_module_information_t) * BOOT_MODULES_MAX_COUNT);
    memcpy(new_bki->params, bki.params, sizeof(char) * PARAMS_BUFFER_SIZE);

    mmap_copy_to_new_location(&bootstrap_mmap, new_bki->mmap);

    bsp_ser_putc('B');

    bsp_ser_putc('A');


	/* Done, return boot info so it can be passed to kmain(). */
	return new_bki;
}

/* pre_init gets executed at the memory location where the kernel was loaded by the boot loader.
 * at that stage we only have a minimum set of functionality present (all symbols gets renamed to
 * ensure this). The following methods are used in that context. Once we jump to kmain they are no
 * longer used and the "real" implementations are visible
 */
void send_diag_sig(void) { }
void minix_shutdown(int how) { arch_shutdown(how); }
void busy_delay_ms(int x) { }
int raise(int n) { panic("raise(%d)\n", n); }
int kern_phys_map_ptr( phys_bytes base_address, vir_bytes io_size, int vm_flags,
struct kern_phys_map * priv, vir_bytes ptr) { return -1; };
struct machine machine; /* pre init stage machine */
