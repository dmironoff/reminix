__asm__(".arch armv7-a\n\t.arch_extension virt"); // Расширенный набор команд для работы с режимом гипервизора, что бы из него выйти

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
#include "pg_utils.h"

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

// Код трамплина
extern char _smp_trampoline_start, _smp_trampoline_end;

// Эти данные нам потребуются для перемещения ядра в памяти
extern uint32_t _kern_phys_base, _kern_vir_base, _kern_size;


static phys_bytes fdt_addr; // Сохраним сюда адрес fdt переданным нам при загрузке


// Карта памяти для преинициализации системы
static mmap_t boot_mmap;
static mmap_region_t boot_mmap_regions[BOOTSTRAP_MMAP_REGIONS];

// Временная структура данных для передачи в основное ядро
bootstrap_kernel_information_t bki;


/*
 * Вывод строки в наш серийный порт, который мы используем для стартовой инициализации
 */
void ser_print(const char *str) {
    while (*str != "\0") {
        bsp_ser_putc(*str);
        str++;
    }
}

/*
 * Вывод человекочитаемого(ASCII) шестнадцатеричного числа в серийный порт
 */
void ser_print_hex(uint32_t value) {
    bsp_ser_putc('0');
    bsp_ser_putc('x');
    for (int i = 7; i >= 0; i--) {
        uint32_t shifted_value = value >> (i * 4);
        uint8_t nibble = shifted_value & 0xF;
        if (nibble < 10) {
            bsp_ser_putc('0' + nibble);
        } else {
            bsp_ser_putc('A' + (nibble - 10));
        }
    }
}

/*
 * Вывод в серийный порт значения переменной в формате "имя: значение"
 */
void ser_print_variable(const char name, uint32_t value) {
    ser_print(name);
    ser_print(": ");
    ser_print_hex(value);
    ser_print("\r\n");
}

/*
 * Функция для вывода критических ошибок в функции pre_init
 * Так как стартовый код большой, то у нас есть местная функция паники
 */
void pre_panic(const char message) {
    ser_print("KERNEL PANIC\r\n");
    ser_print(message);
    ser_print("\r\n");
    while (1) {
        asm volatile ("");
    }
}

/*
 * Функция преобразования строки c типом модуля из fdt в наш ENUM
 *
 */
boot_module_type_t fdt2module_type(const char *str) {
    if (strcmp(str, "server") == 0) {
        return BOOT_MODULE_SERVER;
    }
    if (strcmp(str, "service") == 0) {
        return BOOT_MODULE_SERVICE;
    }
    if (strcmp(str, "driver") == 0) {
        return BOOT_MODULE_DRIVER;
    }
    if (strcmp(str, "virfs") == 0) {
        return BOOT_MODULE_VIRFS;
    }
    if (strcmp(str, "fs") == 0) {
        return BOOT_MODULE_FS;
    }
    if (strcmp(str, "init") == 0) {
        return BOOT_MODULE_INIT;
    }
    if (strcmp(str, "userproc") == 0) {
        return BOOT_MODULE_USERPROC;
    }
    if (strcmp(str, "config") == 0) {
        return BOOT_MODULE_CONFIG;
    }
    return BOOT_MODULE_UNKNOWN;
}

/*
 * Мапинг текущей mmap в apt 1 к 1
 */
static void mmap2apt (mmap_t *mmap, vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table) {
    mmap_region_t *iter;
    uint32_t flags;
    for (iter = mmap->first_region; iter != 0; iter = (mmap_region_t *) iter->next) {
        switch (iter->type) {
            case MMAP_DEVICE:
            case MMAP_DMA:
                flags = VM_APF_DEVICE | VM_APF_PRESENT | VM_APF_DEV_SHARE_W | VM_APF_RW;
                apt_map_phys_to_vir(apt, table, iter->start, iter->size, (vir_bytes) iter->start, flags, iter->cache_hint);
                break;
            case MMAP_KERNEL:
                flags = VM_APF_KERNEL | VM_APF_PRESENT | VM_APF_RWX;
                apt_map_phys_to_vir(apt, table, iter->start, iter->size, (vir_bytes) iter->start, flags, iter->cache_hint);
                break;
            default:
                flags = VM_APF_KERNEL | VM_APF_PRESENT | VM_APF_RWX;
                apt_map_phys_to_vir(apt, table, iter->start, iter->size, (vir_bytes) iter->start, flags, iter->cache_hint);
                break;
        }
    }
}


#pragma clang diagnostic push
#pragma ide diagnostic ignored "UnusedValue"
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
 *
 * Порядок действий:
 * 1. Сохранить аргумент с адресом fdt
 * 2. Сбросить все настройки железа в нужное нам состояние - MMU Disabled + Mode SVC
 * 3. Разметить во временном mmap все регионы переданные в FDT
 * 4. Разметить во временном mmap все регионы загруженных модулей
 * 5. Разметить во временной mmap нахождение ядра и fdt
 * 6. Переместить ядро в его рабочую область
 * 7. Создать временную физическую таблицу страниц с мапингом устройств и физической памяти 1 к 1 для регистра ttbr0
 * 8. Выделить память для нового региона mmap
 * 9. Выделить память для нового региона apt
 * 10. Выделить память для новых физических таблиц виртуальной памяти
 * 11. Включить MMU установив контекст ASID на 0
 * 12. Внести в apt все необходимые данные в том числе маппинг 1 к 1
 * 13. Синхронизировать apt с новыми таблицами
 * 14. Переключить MMU на рабочие таблицы физической памяти
 * 15. Собрать структуру для передачи в основное ядро
 * 16. Прыгнуть в ядро
 */
bootstrap_kernel_information_t *pre_init(int argc, char **argv)
{
    int res;
    int node;
    int proplen;
    int boot_module_id = 0;
    uint32_t *reg;
    phys_bytes new_kernel_start = 0;
    mmap_region_t *region;


    /* Так мы теперь используем протокол загрузки linux из u-boot
     * и поэтому сохраним адрес fdt в отдельную переменную
     * Пункт #1
     * */
    asm volatile ("mov %0, r2" : "=r"(fdt_addr));


    /*
     * НАЧНЁМ ПУНКТ #2
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

    // Пункт 2 закончен, мы перевели всю железку в нужное нам для инициализации состояние

	/* Clear BSS */
	memset(&_edata, 0, (u32_t)&_end - (u32_t)&_edata);
    memset(&_kern_unpaged_edata, 0, (u32_t)&_kern_unpaged_end - (u32_t)&_kern_unpaged_edata);

    // Включаем отладочный вывод в серийный порт.
    bsp_ser_init();
    ser_print("ReMinix ARM32 kernel\r\n");


    // Проверка магического числа fdt
    if (!fdt_check_header((void *) fdt_addr)) {
        pre_panic("FDT Blob header check fail");
    }

    // Инициализация стартовой карты памяти
    boot_mmap.regions = boot_mmap_regions;
    boot_mmap.regions_allocated = BOOTSTRAP_MMAP_REGIONS;
    boot_mmap.l2_page_size = ARM_L2_SIZE;
    node = fdt_path_offset((void *) fdt_addr, "/memory");
    if (node < 0) {
        node = fdt_node_offset_by_prop_value((void *) fdt_addr, 0, "device_type", "memory", 6);
        if (node < 0) {
            ser_print_variable("node", node);
            pre_panic("Can not get memory information from FDT");
        }
    }
    reg = (uint32_t *) fdt_getprop((void *) fdt_addr, node, "reg", &proplen);
    if (proplen <= 0) {
        ser_print_variable("proplen", proplen);
        pre_panic("Can not get memory information from FDT");
    } else {
        // Я стараюсь выделять всё в маленькие регионы кода, что бы не плодить много переменных в области видимости функции
        bsp_devices_mmap_t *devices_map;
        uint32_t *devices_map_count;
        bsp_devices_mmap (devices_map, devices_map_count);
        phys_bytes devices_mem_len;
        phys_bytes mem_len = (phys_bytes) fdt32_to_cpu((fdt32_t)reg[1]);

        for (int i = 0; i < devices_map_count; i++) {
            devices_mem_len += devices_map[i].size;
        }

        res = mmap_init(&boot_mmap, ARCH_L2_PAGE_SIZE, devices_mem_len + mem_len);
        if (res < 0) {
            ser_print_variable("mmap_init", res);
            ser_print_variable("devices_mem_len", devices_mem_len);
            ser_print_variable("mem_len", mem_len);
            pre_panic("Can not init mmap");
        }

        for (int i = 0; i < devices_map_count; i++) {
            mmap_region_t *device;
            res = mmap_alloc_device(&boot_mmap, devices_map[i].start, devices_map[i].size, device);
            if (res < 0) {
                ser_print_variable("mmap_alloc_device", res);
                ser_print_variable("devices_mem_len", devices_mem_len);
                ser_print_variable("mem_len", mem_len);
                ser_print_variable("start", devices_map[i].start);
                ser_print_variable("size", devices_map[i].size);
                pre_panic("Can not map device region");
            }

        }

        // Сразу не отходя от кассы мы разметим в памяти где у нас лежит FDT BLOB
        res = mmap_alloc_region(&boot_mmap, fdt_addr, (phys_bytes) fdt_totalsize((void *) fdt_addr), region);
        if (res < 0) {
            ser_print_variable("mmap_alloc_region", res);
            ser_print_variable("start", fdt_addr);
            ser_print_variable("size", fdt_totalsize((void *) fdt_addr));
            pre_panic("Can not map FDT region");
        }
        region->type = MMAP_FDT;
        bki.fdt_addr = fdt_addr;
        bki.modules[boot_module_id].type = BOOT_MODULE_FDT;
        bki.modules[boot_module_id].name = "FDT";
        bki.modules[boot_module_id].addr = region->start;
        bki.modules[boot_module_id].size = region->size;
        boot_module_id++;

        // Разметим текущее местонахождение ядра _kern_phys_base, _kern_size;
        res = mmap_alloc_region(&boot_mmap, (phys_bytes) _kern_phys_base, (phys_bytes) _kern_size, region);
        if (res < 0) {
            ser_print_variable("mmap_alloc_region", res);
            ser_print_variable("start", fdt_addr);
            ser_print_variable("size", fdt_totalsize((void *) fdt_addr));
            pre_panic("Can not map BOOT_MOD KERNEL region");
        }
        region->type = MMAP_BOOT_MOD; //Регион с ядром откуда мы стартовали у нас будет помечен как загрузочный модуль
        bki.modules[boot_module_id].type = BOOT_MODULE_KERNEL;
        bki.modules[boot_module_id].name = "kernel";
        bki.modules[boot_module_id].addr = region->start;
        bki.modules[boot_module_id].size = region->size;
        boot_module_id++;
    }

    // Пробежимся по модулям
    node = fdt_path_offset((void *) fdt_addr, "/reminiximages");
    if (node < 0) {
        ser_print_variable("node", node);
        pre_panic("Can not get modules information from FDT");
    } else {
        int subnode;
        fdt_for_each_subnode(subnode, (void *) fdt_addr, node) {
            uint32_t addr, size;
            int len;
            const char *name = fdt_get_name((void *) fdt_addr, subnode, &proplen);
            const char *type = fdt_getprop((void *) fdt_addr, subnode, "type", &len);
            if (len <= 0) {
                ser_print_variable("fdt_getprop", len);
                pre_panic("FDT /reminiximages section error: can not get module type.");
            }
            reg = (uint32_t *) fdt_getprop((void *) fdt_addr, subnode, "addr", &len);
            if (len <= 0) {
                ser_print_variable("fdt_getprop", len);
                pre_panic("FDT /reminiximages section error: can not get module addr.");
            }
            addr = fdt32_to_cpu(reg[0]);
            reg = (uint32_t *) fdt_getprop((void *) fdt_addr, subnode, "size", &len);
            if (len <= 0) {
                ser_print_variable("fdt_getprop", len);
                pre_panic("FDT /reminiximages section error: can not get module size.");
            }
            size = fdt32_to_cpu(reg[0]);
            res = mmap_alloc_region(&boot_mmap, (phys_bytes) addr, (phys_bytes) size, region);
            if (res < 0) {
                ser_print_variable("mmap_alloc_region", res);
                pre_panic("FDT /reminiximages section error: can not map memory region");
            }
            region->type = MMAP_BOOT_MOD;
            if (strcmp(name, "vm")) {
                bki.modules[boot_module_id].type = BOOT_MODULE_VM;
            } else {
                bki.modules[boot_module_id].type = fdt2module_type(type);
            }
            strcpy(bki.modules[boot_module_id].name, name);
            bki.modules[boot_module_id].addr = region->start;
            bki.modules[boot_module_id].size = region->size;
            boot_module_id++;
        }
    }

    mmap_region_t *new_kernel_region;
    // Приступаем к пункту 6 - перемещаем ядро
    res = mmap_alloc_lowest_region(&boot_mmap, _kern_size, new_kernel_region);
    if (res < 0) {
        ser_print_variable("mmap_alloc_lowest_region", res);
        pre_panic("Can not map new region for kernel");
    }
    new_kernel_region->type = MMAP_KERNEL;
    new_kernel_start = new_kernel_region->start;
    memset((void *) new_kernel_region->start, 0, new_kernel_region->size); // Вычистим под ядрышко нашу площадочку
    memcpy((void *) new_kernel_region->start, (void *) _kern_phys_base, sizeof(char) * _kern_size);

    // Выделяем память для динамических структур ядра
    // Инициализировать будем после их разметки во временные таблицы памяти и включения MMU
    mmap_region_t *new_mmap_region;         // Регион с основной структурой карты памяти
    mmap_region_t *new_mmap_regions_region; // Регион с пуллом записей о регионах памяти
    mmap_region_t *new_apt_region;         // Основная структура абстрактной таблицы страниц
    mmap_region_t *new_apt_tables_region;
    mmap_region_t *new_apt_l1_region;    // Пулл записей l1
    mmap_region_t *new_apt_l2_region;    // Пулл записей l2
    mmap_region_t *new_pt_handlers_region;  // Регион под массив структур описывающих физические таблицы
    mmap_region_t *new_pt_start_l1_region;  // Регион под первую таблицу l1
    mmap_region_t *new_pt_start_l2_region;  // Регион под первую таблицу l2
    mmap_region_t *new_bki_region; // Результаты инициализации передаваемые ядру
    mmap_region_t *new_smp_trampoline; // Трамплин для старта многоядерных систем

    res = mmap_alloc_lowest_region(&boot_mmap, mmap_align(&boot_mmap, sizeof(mmap_t)), new_mmap_region);
    if (res < 0) {
        ser_print_variable("mmap_alloc_lowest_region", res);
        pre_panic("Can not map new region for new mmap");
    }

    res = mmap_alloc_lowest_region(&boot_mmap, mmap_align(&boot_mmap, sizeof(mmap_t) * BOOTSTRAP_MMAP_REGIONS), new_mmap_regions_region);
    if (res < 0) {
        ser_print_variable("mmap_alloc_lowest_region", res);
        pre_panic("Can not map new region for new mmap regions pull");
    }

    res = mmap_alloc_lowest_region(&boot_mmap, mmap_align(&boot_mmap, sizeof(vm_abstract_pagetables_t)), new_apt_region);
    if (res < 0) {
        ser_print_variable("mmap_alloc_lowest_region", res);
        pre_panic("Can not map new region for new apt mmap");
    }

    res = mmap_alloc_lowest_region(&boot_mmap, mmap_align(&boot_mmap, sizeof(vm_abstract_pt_t) * BOOTSTRAP_APT_COUNT), new_apt_tables_region);
    if (res < 0) {
        ser_print_variable("mmap_alloc_lowest_region", res);
        pre_panic("Can not map new region for new apt tables pull mmap");
    }

    res = mmap_alloc_lowest_region(&boot_mmap, mmap_align(&boot_mmap, sizeof(vm_abstract_pt_l1_entry_t ) * BOOTSTRAP_APT_L1_COUNT), new_apt_l1_region);
    if (res < 0) {
        ser_print_variable("mmap_alloc_lowest_region", res);
        pre_panic("Can not map new region for new apt l1 pull mmap");
    }

    res = mmap_alloc_lowest_region(&boot_mmap, mmap_align(&boot_mmap, sizeof(vm_abstract_pt_l2_entry_t ) * BOOTSTRAP_APT_L2_COUNT), new_apt_l2_region);
    if (res < 0) {
        ser_print_variable("mmap_alloc_lowest_region", res);
        pre_panic("Can not map new region for new apt l1 pull mmap");
    }

    res = mmap_alloc_lowest_region(&boot_mmap, mmap_align(&boot_mmap, sizeof(arm_pt_t) * ARM_MAX_PT_HANDLES), new_pt_handlers_region);
    if (res < 0) {
        ser_print_variable("mmap_alloc_lowest_region", res);
        pre_panic("Can not map new region for new apt l1 pull mmap");
    }

    res = mmap_alloc_lowest_region(&boot_mmap, mmap_align(&boot_mmap, sizeof(bootstrap_kernel_information_t)), new_bki_region);
    if (res < 0) {
        ser_print_variable("mmap_alloc_lowest_region", res);
        pre_panic("Can not map new region for new BKI structure");
    }

    res = mmap_alloc_lowest_region(&boot_mmap, mmap_align(&boot_mmap, (phys_bytes) &_smp_trampoline_end - &_smp_trampoline_start), new_smp_trampoline);
    if (res < 0) {
        ser_print_variable("mmap_alloc_lowest_region", res);
        pre_panic("Can not map new region for smp trampoline");
    }
    // Сразу скопируем трамплин в новое место
    memcpy((void *) new_smp_trampoline->start, (void *) &_smp_trampoline_start, &_smp_trampoline_end - &_smp_trampoline_start);

    res = mmap_alloc_lowest_region(&boot_mmap, mmap_align(&boot_mmap, sizeof(uint32_t) * ARM_L1_ENTRIES), new_pt_start_l1_region);
    if (res < 0) {
        ser_print_variable("mmap_alloc_lowest_region", res);
        pre_panic("Can not map new region for first arm l1 pagetable");
    }

    res = mmap_alloc_lowest_region(&boot_mmap, mmap_align(&boot_mmap, sizeof(uint32_t) * ARM_L1_ENTRIES * ARM_L2_ENTRIES), new_pt_start_l2_region);
    if (res < 0) {
        ser_print_variable("mmap_alloc_lowest_region", res);
        pre_panic("Can not map new region for first arm l2 pagetable");
    }

    // Создаём инициализационную карту памяти и включаем MMU
    pg_init(&boot_mmap);
    vir_bytes vir_addr_mmap = pg_map_high(new_mmap_region);
    vir_bytes vir_addr_mmap_regions = pg_map_high(new_mmap_regions_region);
    vir_bytes vir_addr_apt = pg_map_high(new_apt_region);
    vir_bytes vir_addr_apt_tables = pg_map_high(new_apt_tables_region);
    vir_bytes vir_addr_apt_l1 = pg_map_high(new_apt_l1_region);
    vir_bytes vir_addr_apt_l2 = pg_map_high(new_apt_l2_region);
    vir_bytes vir_addr_pt_handlers = pg_map_high(new_pt_handlers_region);
    vir_bytes vir_addr_pt_l1 = pg_map_high(new_pt_start_l1_region);
    vir_bytes vir_addr_pt_l2 = pg_map_high(new_pt_start_l2_region);
    vir_bytes vir_addr_new_bki = pg_map_high(new_bki_region);
    write_ttbr0(pg_get_phys_addr());
    vm_enable_paging();

    // Осталось инициализировать все структуры
    // Внести текущие данные в apt в том числе мапинг 1 к 1
    // Создать первую физическую таблицу в виртуальной области ядра
    // Синхронизировать apt и физическую таблицу
    // Переключиться на новую таблицу
    // Внести данные в BKI и передать управление в основную часть ядра



    vm_abstract_pagetables_t *apt = (vm_abstract_pagetables_t *) vir_addr_apt;
    apt->tables = (vm_abstract_pt_t *) vir_addr_apt_tables;
    apt->pagetables_allocated = BOOTSTRAP_APT_COUNT;
    apt->l1_entries = (vm_abstract_pt_l1_entry_t *) vir_addr_apt_l1;
    apt->l1_entries_allocated = BOOTSTRAP_APT_L1_COUNT;
    apt->l2_entries = (vm_abstract_pt_l2_entry_t *) vir_addr_apt_l2;
    apt->l2_entries_allocated = BOOTSTRAP_APT_L2_COUNT;
    apt->l2_page_size = ARM_L2_SIZE;

    arm_pt_t *arm_pagetables = (arm_pt_t *) vir_addr_pt_handlers;
    arm_pagetables[0].proc_ep = 0;
    arm_pagetables[0].l1_table = (uint32_t *) vir_addr_pt_l1;
    arm_pagetables[0].l1_phys = new_pt_start_l1_region->start;
    arm_pagetables[0].l2_tables = (uint32_t *) vir_addr_pt_l2;
    arm_pagetables[0].l2_phys = new_pt_start_l2_region->start;
    arm_pagetables[0].status = PT_USED;

    mmap_t *new_mmap = (mmap_t *) vir_addr_mmap;
    new_mmap->regions = (mmap_region_t *) vir_addr_mmap_regions;
    new_mmap->regions_allocated = BOOTSTRAP_MMAP_REGIONS;
    res = mmap_copy_to_new_location(&boot_mmap, new_mmap);
    if (res < 0) {
        ser_print_variable("mmap_copy_to_new_location", res);
        pre_panic("Can not copy mmap to virtual memory");
    }
    new_mmap->l2_page_size = ARM_L2_SIZE;

    res = mmap_find_region_by_addr(new_mmap, arm_pagetables[0].l1_phys, arm_pagetables[0].l1_table_region);
    if (res < 0) {
        ser_print_variable("mmap_find_region_by_addr", res);
        pre_panic("Can not find mmap region of start l1 pagetable");
    }
    res = mmap_find_region_by_addr(new_mmap, arm_pagetables[0].l2_phys, arm_pagetables[0].l2_tables_region);
    if (res < 0) {
        ser_print_variable("mmap_find_region_by_addr", res);
        pre_panic("Can not find mmap region of start l2 pagetable");
    }

    vm_abstract_pt_t *new_apt_table;
    res = apt_make_clean_table(apt, 0, new_apt_table);
    mmap2apt(new_mmap, apt, new_apt_table); // размапить 1 к 1

	/* Done, return boot info so it can be passed to kmain(). */
	return new_bki;
}
#pragma clang diagnostic pop

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
