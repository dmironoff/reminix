//
// Created by dmironov on 30.03.2026.
//

#ifndef REMINIX_SPINLOCK_H
#define REMINIX_SPINLOCK_H

#include "cpufunc.h"

typedef union {
    uint32_t raw;
    struct {
        uint16_t owner;
        uint16_t next;
    };
} arm_spinlock_t;

typedef struct {
    arm_spinlock_t lock;
    uint32_t cpsr;
} arm_spinlock_irq_t;

extern int is_smp_mode;

// История для многоядерных систем
// Полная реализация spinlock очередью на базе архитектурных фишек arm
#define SPINLOCK_T arm_spinlock_t
#define SPINLOCK_IRQ_T arm_spinlock_irq_t
#define SPINLOCK(name)  arm_spinlock_t name
#define EXT_SPINLOCK(name) extern SPINLOCK_T name
#define spinlock_init(sl) sl.raw = 0; sl.owner = 0; sl.next = 0
#define spinlock_lock(sl) if (is_smp_mode) arm_spin_lock(&sl); else arm_irq_disable()
#define spinlock_unlock(sl) if (is_smp_mode) arm_spin_unlock(&sl); else arm_irq_enable()
#define spinlock_trylock(sl) arm_spin_trylock(&sl)
#define SPINLOCK_IRQ(name) arm_spinlock_irq_t name
#define EXT_SPINLOCK_IRQ(name)  extern SPINLOCK_IRQ_T name
#define spinlock_irq_init(sl) sl.lock.raw = 0; sl.lock.owner = 0; sl.lock.next = 0; sl.cpsr = 0
#define spinlock_irq_lock(sl) if (is_smp_mode) arm_irq_spin_lock(&sl); else arm_irq_disable()
#define spinlock_irq_unlock(sl) if (is_smp_mode) arm_irq_spin_unlock(&sl); else arm_irq_enable()


// Это удобный макрос для использования big kernel lock а smp и просто отключения прерываний на UP
// Я его вынес в архитектурнозависимый код для удобства, вдруг на других архитектурах есть более удобные штуки
EXT_SPINLOCK(big_kernel_lock);
#define BKL_LOCK() if (is_smp_mode) arm_spin_lock(&big_kernel_lock); else arm_irq_disable()
#define BKL_UNLOCK() if (is_smp_mode) arm_spin_unlock(&big_kernel_lock); else arm_irq_enable()

// Я планирую реализовать работу планировщика на одном ядре и многоядерном в разном варианте запуска
// Я всёравно на всякий случай прописал работу спинлока для одноядерного а8, потому что сначала не планировал так))))))
// Но потом до меня дошло что и a7/a15 бывают одноядерными и тогда wfe-sev вызовет пустые циклы(от кванта до кванта) работы процессора

/* TODO: Проверить как компилятор это скомпилирует и если нужно переписать это на чистом ассемблере */

static inline void arm_spin_lock(arm_spinlock_t *sl) {
#ifdef ARCH_ARM_CORTEX_A7
    uint32_t tmp, ticket, status = 1;
    // Берём талон, как в поликлинике, и ждём приёма
    asm volatile ("1: \n"
                  "ldrex  %[tmp], [%[lock]] \n"
                  "add %[ticket], %[tmp], #0x10000 \n"
                  "strex %[status], %[ticket], [%[lock]] \n"
                  "teq %[status], #0 \n"
                  "bne 1b \n"
                  "dmb \n"
                  "lsr %[ticket], %[tmp], #16 \n"
                  "uxth %[tmp], %[tmp] \n"
                  "2: \n"
                  "cmp %[ticket], %[tmp] \n"
                  "beq 3f \n"
                  "wfe \n"
                  "ldrh %[tmp], [%[lock]] \n"
                  "b 2b \n"
                  "3: \n"
                  "dsb \n"
                  : [tmp]"=&r"(tmp), [ticket]"=&r"(ticket), [status]"+r"(status)
                  : [lock]"r"(sl)
                  : "memory", "cc");
#endif
#ifdef ARCH_ARM_CORTEX_A8
    arm_irq_disable();
#endif
}

static inline uint32_t arm_spin_trylock(arm_spinlock_t *sl) {
if (!is_smp_mode) {
    arm_irq_disable();
    return 1;
}
#ifdef ARCH_ARM_CORTEX_A7
    uint32_t ticket, result = 0;
    asm volatile ("1: \n"
                  "ldrex %[ticket], [%[lock]] \n"
                  "ror %[result], %[ticket], #16 \n"
                  "eors %[result], %[result], %[ticket] \n"
                  "lsls %[result], %[result], #16 \n"
                  "bne 2f \n"
                  "add %[ticket], %[ticket], #0x10000 \n"
                  "strex %[result], %[ticket], [%[lock]] \n"
                  "teq %[result], #0 \n"
                  "bne 1b \n"
                  "dmb \n"
                  "mov %[result], #1 \n"
                  "2: \n"
                  "clrex \n"
                  "mov %[result], #0 \n"
                  "3: \n"
                  "dsb \n"
                  : [ticket]"=&r"(ticket), [result]"=&r"(result)
                  : [lock]"r"(sl)
                  : "memory", "cc");
    return 0;
#endif
#ifdef ARCH_ARM_CORTEX_A8
    arm_irq_disable();
    return 1;
#endif
}

static inline void arm_spin_unlock(arm_spinlock_t *sl) {
#ifdef ARCH_ARM_CORTEX_A7
    asm volatile ("dmb \n"
                  "ldr r0, [%[owner]] \n"
                  "add r0, r0, #1 \n"
                  "str r0, [%[owner]] \n"
                  "dsb \n"
                  "sev \n"
                  :
                  : [owner]"r"(&sl->owner)
                  : "memory", "r0");

#endif
#ifdef ARCH_ARM_CORTEX_A8
    arm_irq_enable();
#endif
}

static inline void arm_irq_spin_lock(arm_spinlock_irq_t *sl) {
    sl->cpsr = read_cpsr();
    arm_spin_lock(&sl->lock);
}

static inline void arm_irq_spin_unlock(arm_spinlock_irq_t *sl) {
    arm_spin_unlock(&sl->lock);
    write_cpsr(sl->cpsr);
}


#endif //REMINIX_SPINLOCK_H
