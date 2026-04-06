//
// Created by dmironov on 02.04.2026.
//

#ifndef REMINIX_RESMP_H
#define REMINIX_RESMP_H

#include "kernel/kernel.h"
#include "arch_smp.h"
#include "spinlock.h"
#include "kernel/ipi.h"

EXT_SPINLOCK(big_kernel_lock);

// Глобальный флаг и переменная с количеством процессоров, которые мы кучи раз будем проверять
// Объявлены в main.c
extern int is_smp_mode; // 0 - Uniprocessor mode, 1 - SMP
extern int cpu_count; // По умолчанию у нас всего один cpu
extern int bsp_cpu_nr; // Номер процессора на котором мы запустились

#define cpu_is_bsp(nr) (bsp_cpu_nr == nr)

#define CPU_IS_BSP	1
#define CPU_IS_READY	2

struct cpu {
    u32_t flags;
};

EXTERN struct cpu cpus[CONFIG_MAX_CPUS];

#define cpu_set_flag(cpu, flag)	do { cpus[cpu].flags |= (flag); } while(0)
#define cpu_clear_flag(cpu, flag) do { cpus[cpu].flags &= ~(flag); } while(0)
#define cpu_test_flag(cpu, flag) (cpus[cpu].flags & (flag))
#define cpu_is_ready(cpu) cpu_test_flag(cpu, CPU_IS_READY)

// Приколюшка для быстрого определения текущего ядра
#define cpunr cpuid2cpunr(arch_get_current_cpuid())

#endif //REMINIX_RESMP_H
