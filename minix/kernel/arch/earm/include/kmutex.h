//
// Created by dmironov on 18.04.2026.
//

/*
 * Основной прикол этого примитива, что у ядра нет функции lock - она только для процесса
 * У ядра всегда должна быть стратегия на случай занятого мьютекса
 * То есть ядро вызывает trylock, если занято то идёт заниматься другими задачами
 * В этом главный прикол этого примитива
 */


#ifndef REMINIX_KMUTEX_H
#define REMINIX_KMUTEX_H

#include "cpufunc.h"

extern is_smp_mode;

typedef union {
    uint32_t raw;
    struct {
        uint16_t mutex;
        uint16_t kernel;  // 1 - владелец ядро, 0 - владелец сервер
    };
} kmutex_t;

#define kmutex_init(m)  asm volatile ("dmb ish" ::: "memory"); m.mutex = 0; m.kernel = 0
#define kmutex_unlock(m) arch_kmutex_unlock (m)
#define kmutex_trylock(m)   arch_kmutex_trylock (m)
#define kmutex_force_unlock(m) arch_kmutex_force_unlock(m)
#define kmutex_is_kernel(m) m.kernel

inline void arch_kmutex_unlock (kmutex_t *m) {
    if (!is_smp_mode) {
        arm_irq_disable();
        if (m->kernel) {
            m->kernel = 0;
            m->mutex = 0;
        }
        arm_irq_enable();
        return;
    }
    asm volatile ("1:"
                  "ldrex r0, [%[ptr]] \n"  // читаем через монитор
                  "cmp r0, #0x10001 \n"   // установлены 16й бит и 1й
                  "bne 2f \n"   // если не равно, то прёмся дальше к сбросу монитора и выходу
                  "strex r1, #0, [%[ptr]] \n" // пытаемся записать новое значение
                  "cmp r1, #0 \n" // записали?
                  "bne 1b \n"  // если нихуя, то пробуем снова
                  "dmb ish \n" // барьер памяти
                  "b 3f \n" // сваливаем сразу на выход вместо сброса монитора
                  "2:"
                  "clrex \n"  // сброс монитора
                  "3:"
            :: [ptr]"r"(m));
}

inline void arch_kmutex_force_unlock (kmutex_t *m) {
    if (!is_smp_mode) {
        arm_irq_disable();
        m->kernel = 0;
        m->mutex = 0;
        arm_irq_enable();
        return;
    }

    asm volatile ("1:"
                  "ldrex r0, [%[ptr]] \n"
                  "strex r1, #0, [%[ptr]] \n"
                  "cmp r0, #0 \n"
                  "bne 1b \n"
                  "dmb ish \n"
            :
    : [ptr] "r" (m));
}

inline int arch_kmutex_trylock (kmutex_t *m) {
    uint32_t r = 0;
    if (!is_smp_mode) {
        arm_irq_disable();
        if (m->mutex) {
            r = 0;
        } else {
            m->mutex = 1;
            m->kernel = 1;
            r = 1;
        }
        arm_irq_enable();
        return (int)r;
    }

    asm volatile ("1:"
                  "ldrex %[res], [%[ptr]] \n"
                  "cmp %[res], #0 \n"
                  "bne 2f \n"
                  "strex r0, #0x10001, [%[ptr]] \n"
                  "cmp r0, #0 \n"
                  "bne 1b \n"
                  "dmb ish \n"
                  "b 3f \n"
                  "2: \n"
                  "clrex \n"
                  "3: \n"
            : [res] "=r" (r)
    : [ptr] "r" (m));

    return !(int)r;
}

#endif //REMINIX_KMUTEX_H
