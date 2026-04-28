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
    vir_bytes                      fdt_addr; // Для x86 мы сюда кладём только информацию о загруженных модулях

    uint32_t                        system_cpu_count;
    uint32_t                        boot_cpu_number;
    phys_bytes                      smp_trampoline; // Адрес трамплина для многоядерных систем
    vm_abstract_pagetables_t        *apt;  // Хочу обратить внимание что здесь должны быть уже виртуальные адреса
    mmap_t                          *mmap;
    vir_bytes                       arch_pagetables;

    boot_module_information_t       modules[BOOT_MODULES_MAX_COUNT];

    char                            params[PARAMS_BUFFER_SIZE];


    // Размеченные в pre_init регионы памяти
    mmap_region_t                  *kernel_region;
    mmap_region_t                  *mmap_region;
    mmap_region_t                  *apt_region;
    mmap_region_t                  *bki_region;
    mmap_region_t                  *fdt_region;
    mmap_region_t                  *arch_pagetables_region;
} bootstrap_kernel_information_t;



#endif //REMINIX_BOOTSTRAP_KERNEL_INFORMATION_H
