//
// Created by dmironov on 19.03.2026.
//

/*
 * Новая информационная структура для передачи из unpaged в основную часть ядра
 * Эту структуру принимает kmain в качестве параметров
 * Нам это нужно, что бы унифицировать процесс загрузки для разных платформ
 * Никакого больше multiboot в ядре
 * Плюс я расширил список возможных к загрузке модулей
 * Что бы была возможность собирать загрузочный образ системы в разных конфигурациях
 */

#ifndef REMINIX_BOOTSTRAP_KERNEL_INFORMATION_H
#define REMINIX_BOOTSTRAP_KERNEL_INFORMATION_H

#include "pagetables.h"

#define BOOT_MODULES_MAX_COUNT              20
#define PARAMS_BUFFER_SIZE                  2048
#define BOOT_MODULES_NAME_MAX_LEN           20

#ifdef _MINIX_SYSTEM

#include <minix/physmemorymap.h>
#include <minix/abstract_pagetables.h>

// Типы модулей в загрузочном образе по порядку запуска
// Сначала запускаются системные серверы - vm, vfs, proc и т.д.
// Потом запускаются сервисы прослойки - ttyd, usbd, fdtd и т.д.
// Потом стартуют драйверы устройств - serial, mmc
// Потом запускаются сервисы файловых систем - mfs, memory, pfs, devfs
// Потом стартует INIT
// А инит запускает пользовательские процессы, если они по какой-то причине находятся в образе системы
typedef enum {
    BOOT_MODULE_UNKNOWN     = 0,
    BOOT_MODULE_SERVER      = 1,
    BOOT_MODULE_SERVICE     = 2,
    BOOT_MODULE_DRIVER      = 3,
    BOOT_MODULE_FS          = 4,
    BOOT_MODULE_INIT        = 5,
    BOOT_MODULE_USERPROC    = 6,
    BOOT_MODULE_CONFIG      = 7,
    BOOT_MODULE_FDT         = 8
} boot_module_type_t;

typedef struct {
    phys_bytes              addr;
    phys_bytes              size;
    boot_module_type_t      type;
    char                    name[BOOT_MODULES_NAME_MAX_LEN];
} boot_module_information_t;

typedef struct {
#ifdef __arm__
    vir_bytes                      fdt_addr;
    arm_pt_t                        *kernel_pt; // Виртуальный адрес отдельной таблицы страниц ядра для регистра ttbr1
#endif

    uint32_t                        kernel_pt_handler; // Не путать со внутренней таблицой ядра из ttbr1, эта таблица для ttbr0

    uint32_t                        system_cpu_count;
    uint32_t                        boot_cpu_number;
    vm_abstract_pagetables_t        *abstract_pagetables;  /// Хочу обратить внимание что здесь должны быть уже виртуальные адреса
    mmap_t                          *mmap;
    vir_bytes                       user_pt_base;
    phys_bytes                      mem_start;
    phys_bytes                      mem_end;

    boot_module_information_t       modules[BOOT_MODULES_MAX_COUNT];

    char                            params[PARAMS_BUFFER_SIZE];

    vir_bytes               vir_kern_start; /* kernel addrspace starts */
    vir_bytes               bootstrap_start, bootstrap_len;
} bootstrap_kernel_information_t;



#endif // _MINIX_SYSTEM

#endif //REMINIX_BOOTSTRAP_KERNEL_INFORMATION_H
