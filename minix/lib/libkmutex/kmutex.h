//
// Created by dmironov on 18.04.2026.
//

#ifndef REMINIX_KMUTEX_H
#define REMINIX_KMUTEX_H

#define KMUTEX_LOCK_SPIN_ITERS  500

typedef union {
    uint32_t raw;
    struct {
        uint16_t mutex;
        uint16_t kernel;  // 1 - владелец ядро, 0 - владелец сервер
    };
} kmutex_t;

#if defined(__arm__) && !defined(__aarch64__)
#include "arm.h"
#endif

#define kmutex_is_kernel(m) m.kernel

/*
 * Захватить ядерный мьютекс
 */
void kmutex_lock(kmutex_t *m);

/*
 * Отпустить ядерный мьютекс
 */
void kmutex_unlock(kmutex_t *m);

/*
 * Попытка захвата
 * Возвращает 1 если успех или 0 при неудаче
 */
int kmutex_trylock(kmutex_t *m);

#endif //REMINIX_KMUTEX_H
