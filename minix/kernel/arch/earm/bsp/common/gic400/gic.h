//
// Created by dmironov on 10.03.2026.
//

#ifndef REMINIX_GIC_H
#define REMINIX_GIC_H

#define GIC400_NUMBER_STORED_IAR_VALUES 125 // количество сохраняемых прерываний для вложенных прерываний
                                            // Костыль - уберу после реализации многоядерности для ARM

void gic400_init(vir_bytes dist_addr, vir_bytes cpuif_add);
int gic400_get_irq();
void gic400_end_irq(int irq);
void gic400_mask(int irq);
void gic400_unmask(int irq);
void gic400_sgi(uint32_t cpu, uint32_t nr);
void gic400_sgi_all(uint32_t nr);

#endif //REMINIX_GIC_H
