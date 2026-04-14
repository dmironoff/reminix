//
// Created by dmironov on 20.03.2026.
//

#ifndef REMINIX_ARCH_CONFIGS_H
#define REMINIX_ARCH_CONFIGS_H

#define BOOTSTRAP_MMAP_REGIONS      256
#define ARM_L1_PAGES                4096

#define BOOTSTRAP_APT_COUNT         (NR_TASKS + NR_PROCS)  // заранее разметим для будущих процессов
#define BOOTSTRAP_APT_L1_COUNT      512 // Нам много не надо, так как в абстрактной таблице мы размечаем диапазонами
#define BOOTSTRAP_APT_L2_COUNT      255 // заранее разметим

#define ARCH_CACHE_LINE_SIZE        64

#define ARCH_L2_PAGE_SIZE           4096;
#define BOOTSTRAP_L2_PULL_SIZE      255; // Используется в файле pg_utils.c для фиксированного размера пула таблиц страниц l2

/* Максимум процессов — совпадает с KERNEL_MAX_PROCS */
#define ARM_MAX_PT_HANDLES  (NR_TASKS + NR_PROCS)


#endif //REMINIX_ARCH_CONFIGS_H
