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


/* String length used for mb_itoa */
#define ITOA_BUFFER_SIZE 20

/* kernel bss */
extern u32_t _edata;
extern u32_t _end;

/* kernel unpaged bss */
extern char _kern_unpaged_edata;
extern char _kern_unpaged_end;

// Код трамплина
extern uint32_t _smp_trampoline_start, _smp_trampoline_end;

// Эти данные нам потребуются для перемещения ядра в памяти
extern uint32_t _kern_phys_base, _kern_vir_base, _kern_size, _smp_trampoline_start;

// Карта памяти для преинициализации системы
static mmap_t boot_mmap;
static mmap_region_t boot_mmap_regions[BOOTSTRAP_MMAP_REGIONS];

// Временная структура данных для передачи в основное ядро
bootstrap_kernel_information_t BKI;

// Временные переменные для качественной сборки
/* SMP globals */
int is_smp_mode = 0;
int cpu_count   = 1;
int bsp_cpu_nr  = 0;
struct kinfo kinfo;		  /* Заглушка для kinfo */

/*
 * Вывод строки в наш серийный порт, который мы используем для стартовой инициализации
 */
void ser_print(const char *str) {
    while (*str != '\0') {
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
void ser_print_variable(const char *name, uint32_t value) {
    ser_print(name);
    ser_print(": ");
    ser_print_hex(value);
    ser_print("\r\n");
}

/*
 * Функция для вывода критических ошибок в функции pre_init
 * Так как стартовый код большой, то у нас есть местная функция паники
 */
void pre_panic(const char *message) {
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
                // Ядро мы перенесём сами
                //flags = VM_APF_KERNEL | VM_APF_PRESENT | VM_APF_RWX;
                //apt_map_phys_to_vir(apt, table, iter->start, iter->size, (vir_bytes) iter->start, flags, iter->cache_hint);
                break;
            default:
                flags = VM_APF_KERNEL | VM_APF_PRESENT | VM_APF_RWX;
                apt_map_phys_to_vir(apt, table, iter->start, iter->size, (vir_bytes) iter->start, flags, iter->cache_hint);
                break;
        }
    }
}



/*
 * На бедную функцию pre_init выпала теперь тяжкая доля:
 * Разобрать fdt
 * Настроить процессор и MMU
 * Выделить память под общую карту памяти, абстрактные таблицы страниц, физические таблицы страниц
 * Разметить все модули в память ядра, для начала, что бы kmain и vm уже занялись их постепенным запуском
 * Включить MMU
 * и прыгнуть в kmain передав ему  bootstrap_kernel_information_t
 *
 * Порядок действий:
 * 1. Сохранить адрес FDT
 * 2. Привести систему в стартовое состояние
 * 3. Разобрать FDT и стартовую карту памяти
 * 4. Включить временный MMU
 * 5. Инициализировать стартовые структуры данных (mmap, bki, apt, arch_pagetables)
 * 6. Прыгнуть в ядро
 */
bootstrap_kernel_information_t *pre_init(int argc, char **argv)
{
    phys_bytes fdt_addr;
    int node, res;
    mmap_region_t *kernel_region;
    mmap_region_t *apt_region;
    mmap_region_t *mmap_region;
    mmap_region_t *arch_pagetables_region;
    mmap_region_t *bki_region;
    mmap_region_t *fdt_region;


    /* Зачистим все данные */
    memset(&_edata, 0, (u32_t)&_end - (u32_t)&_edata);
    memset(&_kern_unpaged_edata, 0, (u32_t)&_kern_unpaged_end - (u32_t)&_kern_unpaged_edata);

    // Мы в head.s переместили наш адрес FDT от u-boot в регистр r11, надеюсь, что memset его не затёр
    asm volatile ("str r11, [%[fdt]]"
                  : [fdt]"r"(&fdt_addr) :: "memory");

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
        // Нет, мне нравится режим гипервизора, просто пока ReMinix не знает что делать с таким богатством
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

    // Включаем отладочный вывод в серийный порт.
    bsp_ser_init();
    ser_print("ReMinix ARM32 kernel\r\n");

    // Итак, мы в стартовом состоянии

    // Начнём с fdt
    ser_print_variable("FDT_ADDR", (uint32_t)fdt_addr);
    // Проверка магического числа fdt
    if (fdt_check_header((void *) fdt_addr) < 0) {
        pre_panic("FDT Blob header check fail");
    }

    // Для начала инициализируем нашу стартовую карту памяти, ведь мы будем её заполнять
    boot_mmap.page_size = ARM_L2_SIZE;
    boot_mmap.regions = boot_mmap_regions;
    boot_mmap.regions_allocated = BOOTSTRAP_MMAP_REGIONS;
    node = fdt_path_offset((void *)fdt_addr, "/memory");
    if (node < 0) {
        node = fdt_node_offset_by_prop_value((void *) fdt_addr, 0, "device_type", "memory", strlen("memory"));
        if (node < 0) {
            ser_print_variable("FDT NODE VALUE:", node);
            pre_panic("Memory node not found in FDT");
        }
    }
    {
        int len;
        fdt32_t *reg;
        reg = fdt_getprop((void *) fdt_addr, node, "reg", &len);
        if (len <= 0) {
            ser_print_variable("Memory reg:", len);
            pre_panic("Can not get memory info from fdt");
        }
        phys_bytes mem_start = fdt32_to_cpu(&reg[0]);
        phys_bytes mem_size = fdt32_to_cpu(&reg[1]);
        bsp_devices_mmap_t devices[10];
        int devices_count;
        bsp_devices_mmap(devices, devices_count);
        for (int i = 0; i < devices_count; i++) {
            mem_size += devices[i].size;
        }
        mmap_init(&boot_mmap, ARM_L2_SIZE, mem_size);
        for (int i = 0; i < devices_count; i++) {
            mmap_region_t device;
            mmap_alloc_device(&boot_mmap, devices[i].start, devices[i].size, &device);
        }

    }

    mmap_alloc_region(&boot_mmap, fdt_addr, mmap_align(&boot_mmap, fdt_totalsize((void *) fdt_addr)), fdt_region);

    // Иницировали карту памяти, теперь заполним её нужным нам содержимым.
    mmap_alloc_region(&boot_mmap, _kern_phys_base, mmap_align(&boot_mmap, _kern_size), kernel_region);
    kernel_region->type = MMAP_KERNEL;

    node = fdt_path_offset((void *)fdt_addr, "/reminiximages");
    if (node < 0) {
        ser_print_variable("ReMinix images node", node);
        pre_panic("Can not get modules info from fdt");
    }
    {
        int subnode;
        int mod = 0;
        fdt_for_each_subnode(subnode, (void *)fdt_addr, node)
        {
            mmap_region_t *module_region;
            const char *name = fdt_get_name((void *)fdt_addr, subnode, NULL);
            const char *type = fdt_getprop((void *)fdt_addr, subnode, "type", strlen("type"));
            fdt32_t *val = fdt_getprop((void *)fdt_addr, subnode, "addr", strlen("addr"));
            phys_bytes addr = fdt32_to_cpu(*val);
            val = fdt_getprop((void *)fdt_addr, subnode, "size", strlen("size"));
            phys_bytes size = fdt32_to_cpu(*val);
            BKI.modules[mod].type = fdt2module_type(type);
            BKI.modules[mod].addr = addr;
            BKI.modules[mod].size = mmap_align(&boot_mmap, size);
            strcpy(BKI.modules[mod].name, name);
            mmap_alloc_region(&boot_mmap, addr, mmap_align(&boot_mmap, size), module_region);
            module_region->type = MMAP_BOOT_MOD;
            mod++;
        }
    }
    node = fdt_path_offset((void *)fdt_addr, "/chosen");
    if (node >= 0) {
        const char *bootargs = fdt_getprop((void *)fdt_addr, node, "bootargs", strlen("bootargs"));
        bootargs_to_params(bootargs, BKI.params);
    }
    mmap_alloc_highest_region(&boot_mmap, mmap_align(&boot_mmap, sizeof(mmap_t) + sizeof(mmap_region_t) * 300), mmap_region);
    mmap_alloc_highest_region(&boot_mmap, mmap_align(&boot_mmap, sizeof(vm_abstract_pagetables_t) + sizeof(vm_abstract_pt_t) * 256 + sizeof(vm_abstract_pt_entry_t) * 256 * 30), apt_region);
    mmap_alloc_highest_region(&boot_mmap, mmap_align(&boot_mmap, sizeof(arm_pt_t) * 260), arch_pagetables_region);
    mmap_alloc_highest_region(&boot_mmap, mmap_align(&boot_mmap, sizeof(bootstrap_kernel_information_t)), bki_region);


    pg_init(&boot_mmap);
    vir_bytes apt_virbase = pg_map_high(apt_region);
    vir_bytes mmap_virbase = pg_map_high(mmap_region);
    vir_bytes arch_pagetables_virbase = pg_map_high(arch_pagetables_region);
    vir_bytes bki_virbase = pg_map_high(bki_region);
    vir_bytes fdt_virbase = pg_map_high(fdt_region);
    write_ttbr0(pg_get_phys_addr());
    vm_arch_enable_paging();

    bootstrap_kernel_information_t *new_bki = (bootstrap_kernel_information_t *) bki_virbase;
    mmap_t *new_mmap = (mmap_t *) mmap_virbase;
    new_mmap->regions = (mmap_region_t *) (mmap_virbase + sizeof(mmap_t));
    new_mmap->regions_allocated = 300;
    mmap_copy_to_new_location(&boot_mmap, new_mmap);

    vm_abstract_pagetables_t *new_apt = (vm_abstract_pagetables_t *) apt_virbase;
    new_apt->page_size = ARM_L2_SIZE;
    new_apt->pages_max_count = ARM_L1_ENTRIES * ARM_L2_ENTRIES;
    new_apt->tables = (vm_abstract_pt_t *) (apt_virbase + sizeof(vm_abstract_pagetables_t));
    new_apt->pagetables_allocated = 256;
    new_apt->entries = (vm_abstract_pt_entry_t *) (apt_virbase + sizeof(vm_abstract_pagetables_t) + sizeof(vm_abstract_pt_t) * 256);
    new_apt->entries_allocated = 256 * 30;

    mmap_find_region_by_addr(new_mmap, apt_region->start, apt_region);
    apt_region->type = MMAP_KRNL_APT;
    mmap_find_region_by_addr(new_mmap, mmap_region->start, mmap_region);
    mmap_region->type = MMAP_KRNL_MMAP;
    mmap_find_region_by_addr(new_mmap, bki_region->start, bki_region);
    bki_region->type = MMAP_BKI;
    mmap_find_region_by_addr(new_mmap, kernel_region->start, kernel_region);
    mmap_find_region_by_addr(new_mmap, fdt_region->start, fdt_region);
    fdt_region->type = MMAP_FDT;
    mmap_find_region_by_addr(new_mmap, arch_pagetables_region->start, arch_pagetables_region);
    arch_pagetables_region->type = MMAP_PAGETABLES;

    memcpy(&BKI, (void *) new_bki, sizeof(bootstrap_kernel_information_t));
    new_bki->kernel_region = kernel_region;
    new_bki->apt_region = apt_region;
    new_bki->bki_region = bki_region;
    new_bki->mmap_region = mmap_region;
    new_bki->fdt_region = fdt_region;
    new_bki->arch_pagetables_region = arch_pagetables_region;

    new_apt->mmap = new_mmap;
    new_apt->apt = new_apt;
    new_bki->fdt_addr = fdt_virbase;
    new_bki->arch_pagetables = arch_pagetables_virbase;
    new_bki->smp_trampoline = (phys_bytes) _smp_trampoline_start;
    new_bki->system_cpu_count = bsp_smp_get_cpu_count();
    new_bki->boot_cpu_number = cpuid2cpunr(bsp_smp_get_current_cpu());

    /* Бля, помоему всё. Полетели */
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
