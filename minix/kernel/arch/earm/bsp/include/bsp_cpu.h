//
// Created by dmironov on 03.04.2026.
//

#ifndef REMINIX_BSP_CPU_H
#define REMINIX_BSP_CPU_H

void bsp_cpus_control_init (void);

void bsp_boot_cpu(int cpu, phys_bytes entry_point);

void bsp_halt_cpu(int cpu);

void bsp_stop_cpu(void);

#endif //REMINIX_BSP_CPU_H
