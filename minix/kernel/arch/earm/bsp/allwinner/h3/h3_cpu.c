//
// Created by dmironov on 03.04.2026.
//

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
#include "cpufunc.h"

#define SOC_CPUCFG_BASE       0x01f01c00
#define SOC_PRCM_BASE         0x01f01400

// Смещения внутри CPUCFG
#define CPUCFG_GEN_CTRL       0x184
#define CPUCFG_PRIV0          0x1a4
#define CPUCFG_CPU_RST(cpu)   (0x40 + (cpu) * 0x40)
#define CPUCFG_RVBA_LO(cpu)   (0x1a4 + (cpu) * 0x4)

// Смещения внутри PRCM
#define PRCM_CPU_PWROFF       0x100
#define PRCM_CPU_PWR_CLAMP(c) (0x140 + (c) * 0x4)

static kern_phys_map cpucfg_phys_map;
static vir_bytes cpucfg_ptr;
static kern_phys_map prcm_phys_map;
static vir_bytes prcm_ptr;

void bsp_cpus_control_init (void) {
    kern_phys_map_ptr((phys_bytes) SOC_PRCM_BASE, (vir_bytes) 1024,
                      VMMF_UNCACHED | VMMF_WRITE, &prcm_phys_map,
                      (vir_bytes) &prcm_ptr);
    kern_phys_map_ptr((phys_bytes) SOC_CPUCFG_BASE, (vir_bytes) 1024,
                      VMMF_UNCACHED | VMMF_WRITE, &cpucfg_phys_map,
                      (vir_bytes) &cpucfg_ptr);
}

void bsp_boot_cpu(int cpu, phys_bytes entry_point) {
    // Стартовая точка
    mmio_write(cpucfg_ptr + CPUCFG_RVBA_LO(cpu), entry_point);
    // Питалово, как на электростуле из фильмов - подаём почуть-чуть
    for (int i = 0xff; i > 0; i >>= 1) {
        mmio_write(prcm_ptr + PRCM_CPU_PWR_CLAMP(cpu), i);
    }

    // Скидываем  состояние питания
    uint32_t pwr_reg = mmio_read(prcm_ptr + PRCM_CPU_PWROFF);
    pwr_reg &= ~(1 << cpu);
    mmio_write(prcm_ptr + PRCM_CPU_PWROFF, pwr_reg);

    // Аппаратно включаем кэш
    uint32_t gen_ctrl = mmio_read(cpucfg_ptr + CPUCFG_GEN_CTRL);
    gen_ctrl |= (1 << cpu);
    mmio_write(cpucfg_ptr + CPUCFG_GEN_CTRL, gen_ctrl);

    // Снимаем сигнал на ножку резета
    mmio_write(cpucfg_ptr + CPUCFG_CPU_RST(cpu), 0);
    uint32_t rst_ctrl = (1 << 1) | (1 << 0);
    mmio_write(cpucfg_ptr + CPUCFG_CPU_RST(cpu), rst_ctrl);

    arch_cpucore_wakeup(); // Будим ядра
}

void bsp_halt_cpu(int cpu) {
    // Ну типо всё в обратной последовательности
    // НЕ ЗАПУСКАТЬ НА ОТКЛЮЧАЕМОМ ЯДРЕ
    // на выключаемом ядре нужно закончить дела, вырубить прерывания, включить wfe

    // сигнал на ножку резета
    mmio_write(cpucfg_ptr + CPUCFG_CPU_RST(cpu), 0);

    // гасим кэш
    uint32_t gen_ctrl = mmio_read(cpucfg_ptr + CPUCFG_GEN_CTRL);
    gen_ctrl &= ~(1 << cpu);
    mmio_write(cpucfg_ptr + CPUCFG_GEN_CTRL, gen_ctrl);

    // Скидываем  состояние питания
    uint32_t pwr_reg = mmio_read(prcm_ptr + PRCM_CPU_PWROFF);
    pwr_reg |= (1 << cpu);
    mmio_write(prcm_ptr + PRCM_CPU_PWROFF, pwr_reg);

    // И вырубаем напряжение
    mmio_write(prcm_ptr + PRCM_CPU_PWR_CLAMP(cpu), 0);

}

void bsp_stop_cpu(void) {
    arm_irq_disable();
    for(;;)
        asm volatile ("wfi" ::: "memory");
}