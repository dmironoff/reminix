//
// Created by dmironov on 03.04.2026.
//

/*
 * Это архитектурно зависимая реализация многоядерности для нашей новой многоядерной системы
 * Процесс запуска: smp_init() -> arch_smp_boot_cpu() -> настройка тактирования, питания, точки входа для ядра -> трамплин ->
 *  -> arch_smp_cpu_second_init() -> smp_ap_init() -> switch_to_user()
 *  И так для каждого ядра.
 */

#include "kernel/kernel.h"

#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <minix/cpufeature.h>
#include <assert.h>
#include <signal.h>
#include <machine/vm.h>
#include <machine/signal.h>
#include <arm/armreg.h>

#include <minix/u64.h>

#include "archconst.h"
#include "arch_proto.h"
#include "kernel/proc.h"
#include "kernel/debug.h"
#include "ccnt.h"
#include "bsp_init.h"
#include "bsp_serial.h"

#include "glo.h"
#include "bsp_cpu.h"
#include "arch_smp.h"
#include "kernel/mmap_utils.h"

extern void smp_ap_init(void);

/*
 * Архитектурно зависимая часть запуска SMP
 * Типа хук из smp_init();
 */
void arch_smp_init() {
    bsp_cpus_control_init();
}

/*
 * Подготовка к запуску ядра процессора
 * Передача управления на запущенном ядре нашему трамплину
 */
void arch_smp_boot_cpu(int cpu_nr, phys_bytes trampoline_entry) {
    bsp_boot_cpu(cpunr2cpuid(cpu_nr), trampoline_entry);
}

/*
 * Физическое отключение одного из ядер
 * НЕ ЗАПУСКАТЬ НА ОТКЛЮЧАЕМОМ ЯДРЕ
 * на выключаемом ядре нужно закончить дела, вырубить прерывания, включить wfe
 */
void arch_smp_halt_cpu(int cpu_nr) {
    bsp_halt_cpu(cpunr2cpuid(cpu_nr));
}

/*
 * Сюда мы должны вылететь из трамплина
 */
void arch_smp_cpu_second_init(void) {
    switch_k_stack((char *)get_k_stack_top(cpuid2cpunr(arch_get_current_cpuid())), smp_ap_init);
}

/*
 * Копирование трамплина в новую область памяти
 * Возвращает физический адрес для arch_smp_boot_cpu()
 */
phys_bytes arch_smp_copy_trampoline(mmap_t *mmap) {
    extern char __k_unpaged__smp_trampoline_end, __k_unpaged__smp_trampoline_start;
    phys_bytes size = &__k_unpaged__smp_trampoline_end - &__k_unpaged__smp_trampoline_start;
    mmap_region_t *region;

    mmap_alloc_lowest_region(mmap, mmap_align(mmap, size), region);
    region->type = MMAP_SMP_TRAMPOLINE;

    memcpy((void *) &__k_unpaged__smp_trampoline_start, (void *) region->start, size);

    return region->start;
}
