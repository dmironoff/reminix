#include <sys/types.h>
#include <machine/cpu.h>
#include <minix/type.h>
#include <minix/board.h>
#include <io.h>

#include "kernel/kernel.h"
#include "kernel/proc.h"
#include "kernel/vm.h"
#include "kernel/proto.h"
#include "arch_proto.h"
#include "hw_intr.h"
#include "gic.h"
#include "bsp_ipi.h"

/*
 * Опять таки эта информация должна получатьс из FDT
 * Но пока мы просто захардкодим эту шляпу здесь
 */
#define GIC_CPUIF_BASE 0x01C82000
#define GIC_DIST_BASE 0x01C82000

int
intr_init(const int auto_eoi)
{
    gic400_init((vir_bytes) GIC_DIST_BASE, (vir_bytes) GIC_CPUIF_BASE);
    return 0;
}

void
bsp_irq_handle(void)
{
	/* Эта функция вызывается из асемблерного кода, который обрабатывает вектор прерываний */
    int irq = gic400_get_irq();
    irq_handle(irq);
	gic400_end_irq(irq);
}

void
bsp_irq_unmask(int irq)
{
    gic400_unmask(irq);
}

void
bsp_irq_mask(const int irq)
{
    gic400_mask(irq);
}

void bsp_send_ipi(uint32_t cpu, uint32_t ipi_nr) {

}

void bsp_send_ipi_all_others(uint32_t ipi_nr) {

}

