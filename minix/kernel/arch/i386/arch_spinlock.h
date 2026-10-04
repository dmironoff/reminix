#ifndef __ARCH_SPINLOCK_H__
#define __ARCH_SPINLOCK_H__

#include <stdatomic.h>

#define MCS_CACHELINE 64  /* TODO Вынести это более глобально в хедер архитектуры, либо найти там макрос со значением длинны линии кеша*/

struct mcs_node {
    _Atomic(struct mcs_node *) next;
    atomic_uint locked;
} __attribute__((aligned(MCS_CACHELINE)));

typedef struct spinlock {
    _Atomic(struct mcs_node *) tail __attribute__((aligned(MCS_CACHELINE)));
    struct mcs_node nodes[CONFIG_MAX_CPUS];
} spinlock_t;

static inline void arch_spinlock_init (spinlock_t *sl) {
    atomic_init(&sl->tail, NULL);
    for (int i = 0; i < CONFIG_MAX_CPUS; i ++) {
        atomic_init(&sl->nodes[i].next, NULL);
        atomic_init(&sl->nodes[i].locked, 0);
    }
}

static inline void arch_spinlock_lock(spinlock_t *sl, uint32_t cpu) {
    struct mcs_node *prev;

    /*
     * TODO проверка повторного захвата. Замки ядра нерекурсивные, и
     * архитектура повторного захвата на одном CPU не допускает: ядро
     * работает с выключенными прерываниями, они включаются только с
     * отпущенным BKL (idle/halt_cpu, выход в пользователя, калибровка
     * i8253), NMI замков не берёт. Но если это всё же случится, MCS не
     * просто зависнет, а испортит очередь: узел CPU уже стоит в ней,
     * next/locked затираются, xchg хвоста вернёт свой же узел, и CPU будет
     * ждать сам себя, а за ним -- все остальные. Проверка по своему узлу
     * (только своя кэш-строка, на фоне xchg ничего не стоит):
     *   - соглашение: locked = 0 -- узел свободен, 1 -- ждём в очереди,
     *     2 -- замок наш (предшественник при передаче пишет 2, а не 0;
     *     ждущий крутится, пока locked == 1); при захвате без очереди
     *     locked остаётся 1 или ставится 2 -- главное, не 0;
     *   - в конце unlock, после передачи замка, вернуть узел в начальное
     *     состояние: next = NULL, locked = 0;
     *   - здесь: if (locked != 0 || next != NULL) panic("re-lock ...").
     * Без смены соглашения проверка не работает: у держателя, которому
     * замок передали и за которым никто не стоит, сейчас locked == 0 и
     * next == NULL -- ровно начальное состояние.
     * Номер CPU верен и на стеке загрузки: BSP всегда логический CPU 0
     * (discover_cpus(), docs/modernization.md п. 4).
     */

    atomic_store_explicit(&sl->nodes[cpu].next, NULL, memory_order_relaxed);
    atomic_store_explicit(&sl->nodes[cpu].locked, 1, memory_order_relaxed);

    prev = atomic_exchange_explicit(&sl->tail, &sl->nodes[cpu], memory_order_acq_rel);
    if (prev == NULL) {
        /* очередь пуста */
        atomic_store_explicit(&sl->nodes[cpu].locked, 0, memory_order_release);
        return;
    }
    atomic_store_explicit(&prev->next, &sl->nodes[cpu], memory_order_release);
    while (atomic_load_explicit(&sl->nodes[cpu].locked, memory_order_acquire))
        __asm__ __volatile__("pause");
}

static inline void arch_spinlock_unlock(spinlock_t *sl, uint32_t cpu) {
    struct mcs_node *next = atomic_load_explicit(&sl->nodes[cpu].next, memory_order_acquire);
    struct mcs_node *expected = &sl->nodes[cpu];

    /*
     * TODO проверка освобождения без захвата. Хуже повторного захвата:
     * cmpxchg хвоста не сработает (хвост -- чужой узел или NULL), и CPU либо
     * ждёт next, который не придёт, либо отдаёт замок чужому ожидающему --
     * в ядре окажутся два CPU сразу (тихая порча, а не зависание). Такая
     * ошибка уже была: «два unlock подряд из idle()» (docs/testing.md,
     * разбор bkl.txt). Проверка при соглашении из TODO в
     * arch_spinlock_lock: if (locked == 0) panic("unlock without lock ...");
     * после передачи замка -- сбросить свой узел (next = NULL, locked = 0).
     */

    if (next == NULL) {
        if (atomic_compare_exchange_strong_explicit(&sl->tail, &expected, NULL,
                                                    memory_order_release, memory_order_relaxed))
            return;
        while ((next = atomic_load_explicit(&sl->nodes[cpu].next, memory_order_acquire)) == NULL)
            __asm__ __volatile__("pause");
    }
    atomic_store_explicit(&next->locked, 0, memory_order_release);
}

static inline int arch_spinlock_try_lock(spinlock_t *sl, uint32_t cpu) {
    struct mcs_node *expected = NULL;
    atomic_store_explicit(&sl->nodes[cpu].next, NULL, memory_order_relaxed);
    atomic_store_explicit(&sl->nodes[cpu].locked, 0, memory_order_relaxed);
    return atomic_compare_exchange_strong_explicit(
            &sl->tail, &expected, &sl->nodes[cpu],
            memory_order_acq_rel,   /* успех: как xchg в mcs_lock */
            memory_order_relaxed);  /* неудача: ничего не публикуем */
}

static inline int arch_spinlock_cpu_is_locked(spinlock_t *sl, uint32_t cpu) {
    if (atomic_load_explicit(&sl->nodes[cpu].locked, memory_order_acquire) == 1) {
        return 1;
    }
    return 0;
}

static inline int arch_spinlock_is_locked(spinlock_t *sl) {
    if (atomic_load_explicit(&sl->tail, memory_order_acquire) != NULL) {
        return 1;
    }
    return 0;
}

#endif /* __ARCH_SPINLOCK_H__ */
