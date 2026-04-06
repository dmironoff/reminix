//
// Created by dmironov on 25.03.2026.
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
#include "bsp_smp_info.h"

// Эта хуита вынесена в функции, а не в define так как на некоторых платформах разное количество ядер
// И их количество можно считать с регистров
uint32_t bsp_smp_get_cpu_count(void) {
    return 4;
}
uint32_t bsp_smp_get_current_cpu(void) {
    uint32_t mpidr;
    // Чтение регистра через копроцессор CP15
    asm volatile("mrc p15, 0, %0, c0, c0, 5" : "=r" (mpidr));

    // Для Cortex-A7 достаточно маски 0x3 (биты 0 и 1)
    return mpidr & 0x03;
}