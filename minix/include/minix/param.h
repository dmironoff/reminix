
#ifndef _MINIX_PARAM_H
#define _MINIX_PARAM_H 1

#include <minix/com.h>
#include <minix/const.h>
#include <minix/physmemorymap.h>
#include <minix/abstract_pagetables.h>

#define BOOT_MODULES_MAX_COUNT              25
#define BOOT_MODULES_NAME_MAX_LEN           20

// Типы модулей в загрузочном образе по порядку запуска
// Сначала запускаются системные серверы - vm, vfs, proc и т.д.
// Потом запускаются сервисы прослойки - ttyd, usbd, fdtd и т.д.
// Потом стартуют виртуальные файловые системы
// Потом стартуют драйверы устройств - serial, mmc
// Потом запускаются сервисы файловых систем - mfs, ext2 и тд
// Потом стартует INIT
// А инит запускает пользовательские процессы, если они по какой-то причине находятся в образе системы
typedef enum {
    BOOT_MODULE_UNKNOWN     = 0,  // Хз что такое
    BOOT_MODULE_SERVER      = 1,  // Сервер
    BOOT_MODULE_SERVICE     = 2,  // Сервис (прокладка между системой и драйвером)
    BOOT_MODULE_DRIVER      = 3,  // Драйвер
    BOOT_MODULE_VIRFS       = 4,  // Виртуальная файловая система - devfs, procfs, sysfs ...
    BOOT_MODULE_FS          = 5,  // Обычная файловая система
    BOOT_MODULE_INIT        = 6,  // Инит
    BOOT_MODULE_USERPROC    = 7,  // Пользовательский процесс
    BOOT_MODULE_CONFIG      = 8,  // Конфигурационный файл
    BOOT_MODULE_FDT         = 9,  // FDT BLOB
    BOOT_MODULE_VM          = 10,   // Сервер виртуальной памяти, он стартует первым
    BOOT_MODULE_KERNEL      = 11  // Загрузочный образ ядра, после инициализации просто удаляется из памяти
} boot_module_type_t;

typedef struct {
    phys_bytes              addr;
    phys_bytes              size;
    boot_module_type_t      type;
    char                    name[BOOT_MODULES_NAME_MAX_LEN];
} boot_module_information_t;

#ifdef _MINIX_SYSTEM
/* This is used to obtain system information through SYS_GETINFO. */
#define MAXMEMMAP 40
#define PARAMS_BUFFER_SIZE                  2048

typedef struct kinfo {

        /* Minix stuff */
        struct kmessages *kmessages;
        int do_serial_debug;    /* system serial output */
        int serial_debug_baud;  /* serial baud rate */
        int minix_panicing;     /* are we panicing? */
        vir_bytes               user_sp; /* where does kernel want stack set */
        vir_bytes               user_end; /* upper proc limit */
        vir_bytes               vir_kern_start; /* kernel addrspace starts */

        int nr_procs;           /* number of user processes */
        int nr_tasks;           /* number of kernel tasks */
        char release[6];        /* kernel release number */
        char version[6];        /* kernel version number */

        char                            params[PARAMS_BUFFER_SIZE];

        // Наши модули, что бы системные серверы их постепенно загружали
        // И брали от сюда информацию
        boot_module_information_t       modules[BOOT_MODULES_MAX_COUNT];

        // Эта информация для VM - что бы он после своего старта вычистил лишнее из памяти
        vir_bytes               bootstrap_start, bootstrap_len;

        // Мы делаем прототипы на этапе преинициализации так как у нас там гораздо больше данных о регионах памяти
        vm_abstract_pt_t                *apt_user_process_prototype;  // Прототипы таблиц для пользовательского процесса
        vm_abstract_pt_t                *apt_vm_process_prototype;  // Для процесса менеджера виртуальной памяти.

        // Заранее разметим несколько секций l1 для копирования данных между ядром и процессами
        // Они будут иметь в таблице страниц отдельный тип
        vir_bytes                      vir_memory_cp_region_addr;
        vir_bytes                      vir_memory_cp_region_size;

        vm_abstract_pagetables_t        *apt;  // Хочу обратить внимание что здесь должны быть уже виртуальные адреса
        mmap_t                          *mmap;

        uint32_t                        system_cpu_count;
        uint32_t                        boot_cpu_number;
        int                             is_smp_mode; // Режим ядра - включён ли SMP
                                                    // У нас больше нет CONFIG_SMP, теперь код SMP всегда собран в ядро и серверы

#ifdef __arm__
        vir_bytes                      fdt_addr; // Для arm у нас будет валяться здесь адрес образа fdt
#endif
} kinfo_t;
#endif /* _MINIX_SYSTEM */

#endif
