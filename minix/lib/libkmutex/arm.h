//
// Created by dmironov on 18.04.2026.
//

#ifndef REMINIX_KMUTEX_ARM32_H
#define REMINIX_KMUTEX_ARM32_H

inline int arch_kmutex_trylock(kmutex_t *m) {
    uint32_t res;
    asm volatile ("1:"
                  "ldrex %[res], [%[ptr]] \n"
                  "cmp %[res], #0 \n"
                  "bne 2f \n"
                  "strex r0, #0x10000, [%[ptr]] \n"
                  "cmp r0, #0 \n"
                  "bne 1b \n"
                  "dmb ish \n"
                  "b 3f \n"
                  "2: \n"
                  "clrex \n"
                  "3: \n"
            : [res] "=r" (res)
    : [ptr] "r" (m));
    return !res;
}

inline void arch_kmutex_unlock(kmutex_t *m) {
    asm volatile ("1:"
                  "ldrex r0, [%[ptr]] \n"  // читаем через монитор
                  "cmp r0, #0x10000 \n"   // установлен 16й бит а 0й свободен
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

#endif //REMINIX_KMUTEX_ARM32_H
