//
// Created by dmironov on 02.04.2026.
//

#ifndef REMINIX_IPI_H
#define REMINIX_IPI_H

#include "kernel/kernel.h"
#include "kernel/proc_context.h"
#include "arch_proc_context.h"
#include "arch_ipi.h"

typedef struct {
    struct proc *proc;
    volatile int  ack_count;
} tlb_shoot_info_t;

typedef struct {
    int         func;
    volatile int         result;
    void        *data;
} ipi_call_info_t;

typedef struct {
    struct proc *proc; // proc descr
    volatile int    result;
} ipi_stop_proc_t;

typedef struct {
    struct proc *proc; // proc descr
    volatile int    result;
} ipi_save_ctx_t;

typedef struct {
    struct proc *proc; // proc descr
    volatile int    result;
} ipi_vm_inhibit_t;

extern uint32_t *arch_ipi_table;
extern void arch_ipi_ack(void);
extern void arch_send_ipi(int cpunr, int ipi);
extern void arch_send_ipi_all_others(int ipi);

#define IPI_NR(nr)  arch_ipi_table[nr]
#define IPI_SEND(cpu, nr) arch_send_ipi(cpu, arch_ipi_table[nr])
#define IPI_SEND_ALL(nr) arch_send_ipi_all_others(arch_ipi_table[nr])
#define IPI_ACK()   arch_ipi_ack()

/*
 * Мы будем использовать это что бы читать карту номеров прерываний для нашего ipi
 */

#define IPI_TLB_SHOOT   0
#define IPI_STOP        1
#define IPI_RESCHEDULE  2
#define IPI_CALL        3
#define IPI_STOP_PROC   4
#define IPI_SAVE_CTX    5
#define IPI_VM_INHIBIT  6

// Флаги для поля ipi_pending в cpu_locals
#define IPI_PENDING_TLB_SHOOT   (1u << 0)
#define IPI_PENDING_STOP        (1u << 1)
#define IPI_PENDING_RESCHEDULE  (1u << 2)
#define IPI_PENDING_CALL        (1u << 3)
#define IPI_PENDING_STOP_PROC   (1u << 4)
#define IPI_PENDING_SAVE_CTX    (1u << 5)
#define IPI_PENDING_VM_INHIBIT  (1u << 6)

#endif //REMINIX_IPI_H
