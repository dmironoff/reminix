#include "smp_tramp.h"

.section .smp_trampoline
_smp_trampoline_entry:
    /* Запрещаем все прерывания */
    cpsid   if

    /* Читаем CPU ID из MPIDR[7:0] */
    mrc     p15, 0, r4, c0, c0, 5       /* r4 = MPIDR              */
    and     r4, r4, #0xFF               /* r4 = CPU ID (0-3)       */

    /* Настраиваем стек:
     * ap_stacks — массив стеков, AP_STACK_SIZE байт на каждый.
     * sp = ap_stacks + (cpu_id + 1) * AP_STACK_SIZE              */
    ldr     r0, =ap_stacks
    ldr     r1, =AP_STACK_SIZE
    mul     r2, r4, r1                  /* r2 = cpu_id * stack_sz  */
    add     r0, r0, r2
    add     sp, r0, r1                  /* sp = начало + размер     */

    /* Выравниваем sp по 8 байтам (требование AAPCS) */
    bic     sp, sp, #7

    /* Включаем кэш данных и инструкций.
     * MMU уже включён BSP с общей kernel page table.
     * AP наследуют те же маппинги через TTBR1.
     * ASID пока 0 — нет TLB конфликтов.                         */
    mrc     p15, 0, r0, c1, c0, 0      /* SCTLR                   */
    orr     r0, r0, #(1 << 2)          /* C: enable D-cache       */
    orr     r0, r0, #(1 << 12)         /* I: enable I-cache       */
    mcr     p15, 0, r0, c1, c0, 0
    isb

    /* Переходим в C */
    bl      arch_smp_cpu_second_init

    /* Сюда не должны добраться */
.Lap_halt:
    cpsid   if
    wfi
    b       .Lap_halt

.size _smp_ap_entry, . - _smp_ap_entry

/*
 * Стеки для AP ядер.
 * BSP использует стек определённый в linker script.
 * AP получают по AP_STACK_SIZE байт каждый.
 * Выравнены по странице (4096 байт) для MPU/guard pages.
 */
.section .bss.ap_stacks
.global ap_stacks
.align  12                              /* выравнивание по странице */

ap_stacks:
.space  (CONFIG_MAX_CPUS * AP_STACK_SIZE)

.size ap_stacks, . - ap_stacks