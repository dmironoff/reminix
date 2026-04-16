//
// Created by dmironov on 01.04.2026.
//

#ifndef REMINIX_IPI_H
#define REMINIX_IPI_H

#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <minix/type.h>
#include "bsp_ipi.h"

uint32_t arch_ipi_table[] = {
        0,  // TLB_SHOOT
        1, // STOP
        2, //RESCHEDULE
        3, // CALL
        4, // STOP_PROC
        5, // SAVE_CTX
        6 // VM_INHIBIT
};


void arch_send_ipi(int cpunr, int ipi);
void arch_send_ipi_all_others(int ipi);
void arch_ipi_ack(void);



#endif //REMINIX_IPI_H
