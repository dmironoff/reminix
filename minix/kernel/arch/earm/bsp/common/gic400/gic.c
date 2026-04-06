//
// Created by dmironov on 10.03.2026.
//

/*
 * Простейший драйвер для контроллера прерываний GIC400
 * Он встречается в куче процессорв, так как является промышленным стандартом
 * Поэтому этот драйвер вынесен в область common
 *
 */

#include <minix/board.h>
#include <io.h>

#include "kernel/kernel.h"
#include "kernel/proc.h"
#include "kernel/vm.h"
#include "kernel/proto.h"
#include "arch_proto.h"
#include "gic.h"
#include "gic_registers.h"

static struct gic400_memory
{
    vir_bytes dist_base;
    vir_bytes cpuif_base;
    int dist_size;
    int cpuif_size;
} gic400_memory;

static kern_phys_map gic400_dist_phys_map;
static kern_phys_map gic400_cpuif_phys_map;

/*
 * Для нас важно записать точное значение регистра IAR
 * в регистр EOIR, когда мы обработаем прерывание
 * Мы моглибы туда вхуячить просто номер, но тогда потеряем часть идентификатора
 * И наш контроллер прерываний сделает какуюнить хуйню
 * Так что пока мы не научились работать с многоядерностью и остальной хуетой мы просто будем
 * временно сохранять эту шляпу во внутреннюю переменную
 * но тут есть определённая хуйня - у нас бывают вложенные прерывания
 * А мы реально не вкурсе
 */
static uint32_t iar_current_value[GIC400_NUMBER_STORED_IAR_VALUES] = { 0 };

/*
 * Инициализация контроллера прерываний
 */
void gic400_init(vir_bytes dist_addr, vir_bytes cpuif_addr) {

    gic400_memory.dist_base = dist_addr;
    gic400_memory.dist_size = GIC400_DIST_MEMORY_SIZE;

    gic400_memory.cpuif_base = cpuif_addr;
    gic400_memory.cpuif_size = GIC400_CPUIF_MEMORY_SIZE;

    kern_phys_map_ptr(gic400_memory.dist_base, gic400_memory.dist_size,
                      VMMF_UNCACHED | VMMF_WRITE,
                      &gic400_dist_phys_map, (vir_bytes) & gic400_memory.dist_base);

    kern_phys_map_ptr(gic400_memory.cpuif_base, gic400_memory.cpuif_size,
                      VMMF_UNCACHED | VMMF_WRITE,
                      &gic400_cpuif_phys_map, (vir_bytes) & gic400_memory.cpuif_base);
}

/*
 * Получение текущего сработавшего прерывания
 */
int gic400_get_irq() {
    uint32_t iar = mmio_read(gic400_memory.cpuif_base + GIC400_CPUIF_IAR);
    int irq_number = iar & GIC400_CPUIF_IAR_IRQ_NUMBER_MASK;
    if (irq_number < 1022) {
        iar_current_value[irq_number] = iar;
    }
    return irq_number;
}

/*
 * Говорим процессору что прерывание обработано
 */

void gic400_end_irq(int irq_number) {
    if (iar_current_value[irq_number] > 0) {
        mmio_write(gic400_memory.cpuif_base + GIC400_CPUIF_EOIR, iar_current_value[irq_number]);
        iar_current_value[irq_number] = 0;
    }
}

/*
 * Отключение прерываний
 */
void gic400_mask(int irq) {
    int reg_number = irq / 32;
    int bit_number = irq % 32;
    mmio_write(gic400_memory.dist_base + GIC400_DIST_ICENABLER + (reg_number * 4), (1 << bit_number));
}

/*
 * Включение прерываний
 */
void gic400_unmask(int irq) {
    int reg_number = irq / 32;
    int bit_number = irq % 32;
    mmio_write(gic400_memory.dist_base + GIC400_DIST_ISENABLER + (reg_number * 4), (1 << bit_number));
}

/*
 * Отправить програмное прерывание
 */
void gic400_sgi(uint32_t cpu, uint32_t nr) {

}

/*
 *  Отправить програмное прерывание всем остальным ядрам
 *
 */

void gic400_sgi_all(uint32_t nr) {

}