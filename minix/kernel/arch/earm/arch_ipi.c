//
// Created by dmironov on 04.04.2026.
//

#include "arch_ipi.h"
#include "arch_smp.h"

void arch_send_ipi(int cpu_nr, int ipi) {
    arch_barrier();
    bsp_send_ipi(cpunr2cpuid(cpu_nr), (uint32_t) ipi);
}

void arch_send_ipi_all_others(int ipi) {
    arch_barrier();
    bsp_send_ipi_all_others((uint32_t) ipi);
}

void arch_ipi_ack(void) {
   return;
}