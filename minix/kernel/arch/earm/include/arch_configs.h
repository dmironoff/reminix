//
// Created by dmironov on 20.03.2026.
//

#ifndef REMINIX_ARCH_CONFIGS_H
#define REMINIX_ARCH_CONFIGS_H

#define BOOTSTRAP_MMAP_REGIONS      1024
#define ARM_KERNEL_L1_PAGES         512  // ну вот так - это что у нас будет загружено в регистр ttbr1
#define ARM_KERNEL_VIRT_START       0xE0000000
#define ARM_USER_L1_PAGES           3584 // Это количество страниц l1 для регистра ttbr0

#define BOOTSTRAP_APT_COUNT         (NR_TASKS + NR_PROCS)  // заранее разметим для будущих процессов
#define BOOTSTRAP_APT_L1_COUNT      512 // Нам много не надо, так как в абстрактной таблице мы размечаем диапазонами
#define BOOTSTRAP_APT_L2_COUNT      255 // заранее разметим

#define KERNEL_APT_INDEX            0   // для arm у нас будет отдельно размечена таблица для ядра

#define ARCH_CACHE_LINE_SIZE        64

/* Максимум процессов — совпадает с KERNEL_MAX_PROCS */
#define ARM_MAX_PT_HANDLES  (NR_TASKS + NR_PROCS)


#endif //REMINIX_ARCH_CONFIGS_H
