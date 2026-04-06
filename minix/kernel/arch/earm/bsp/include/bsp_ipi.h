//
// Created by dmironov on 30.03.2026.
//

#ifndef REMINIX_BSP_IPI_H
#define REMINIX_BSP_IPI_H

void bsp_send_ipi(uint32_t cpu, uint32_t ipi_nr);
void bsp_send_ipi_all_others(uint32_t ipi_nr);


#endif //REMINIX_BSP_IPI_H
