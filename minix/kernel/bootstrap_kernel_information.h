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

#define BOOT_MODULES_MAX_COUNT              25
#define PARAMS_BUFFER_SIZE                  2048
#define BOOT_MODULES_NAME_MAX_LEN           20

#include <minix/physmemorymap.h>
#include <minix/abstract_pagetables.h>

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

typedef struct {
#ifdef __arm__
    vir_bytes                      fdt_addr;
#endif

    int                             kernel_pt_handler; // это таблица для ttbr0
    vm_abstract_pt_t                *kernel_apt; // абстрактная таблица ядра - это всё адресное пространство из ttbr0

    uint32_t                        system_cpu_count;
    uint32_t                        boot_cpu_number;
    phys_bytes                      smp_trampoline; // Адрес трамплина для многоядерных систем
    vm_abstract_pagetables_t        *apt;  // Хочу обратить внимание что здесь должны быть уже виртуальные адреса
    mmap_t                          *mmap;
    vir_bytes                       arch_pt_base;

    boot_module_information_t       modules[BOOT_MODULES_MAX_COUNT];

    char                            params[PARAMS_BUFFER_SIZE];

    vir_bytes               vir_kern_start; /* kernel addrspace starts */
    vir_bytes               bootstrap_start, bootstrap_len;
} bootstrap_kernel_information_t;



#endif //REMINIX_BOOTSTRAP_KERNEL_INFORMATION_H
