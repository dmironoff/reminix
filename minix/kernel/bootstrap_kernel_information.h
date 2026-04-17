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

#include <minix/physmemorymap.h>
#include <minix/abstract_pagetables.h>
#include <minix/param.h>


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

    // Мы делаем прототипы на этапе преинициализации так как у нас там гораздо больше данных о регионах памяти
    vm_abstract_pt_t                *apt_user_process_prototype;  // Прототипы таблиц для пользовательского процесса
    vm_abstract_pt_t                *apt_vm_process_prototype;  // Для процесса менеджера виртуальной памяти.

    // Заранее разметим несколько секций l1 для копирования данных между ядром и процессами
    // Они будут иметь в таблице страниц отдельный тип
    vir_bytes                      vir_memory_cp_region_addr;
    vir_bytes                      vir_memory_cp_region_size;
} bootstrap_kernel_information_t;



#endif //REMINIX_BOOTSTRAP_KERNEL_INFORMATION_H
