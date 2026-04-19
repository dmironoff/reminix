//
// Created by dmironov on 18.04.2026.
//

#include "minix/syslib.h"
#include "kmutex.h"

/*
 * Наш механизм мьютексов в общей памяти с ядром
 * ВСЕГДА! ВСЕГДА Инициализация мьютекса за ядром
 * Хедер для этой библиотеки <minix/kmutex.h>
 * в ядре реализация лежит в kernel/arch/
 * * Основной прикол этого примитива, что у ядра нет функции lock - она только для процесса
* У ядра всегда должна быть стратегия на случай занятого мьютекса
* То есть ядро вызывает trylock, если занято то идёт заниматься другими задачами
* В этом главный прикол этого примитива
* А у процесса есть функция lock которая вызывает несколько холостых тактов и если всё ещё занято, то
* sys_kyield() в цикле проверки мьютекса
*/

/*
 * Попытка захвата
 * Возвращает 1 если успех или 0 при неудаче
 */
int kmutex_trylock(kmutex_t *m) {
    return arch_kmutex_trylock(m);
}


/*
 * Захватить ядерный мьютекс
 */
void kmutex_lock(kmutex_t *m) {
    while (!kmutex_trylock(m)) {
        for (i = 0; i < KMUTEX_LOCK_SPIN_ITERS; i++) {
             asm volatile ("");
        }
        if (!kmutex_trylock(m)) {
            sys_kyield();
        }
    }
};

/*
 * Отпустить ядерный мьютекс
 */
void kmutex_lock(kmutex_t *m) {
    arch_kmutex_unlock(m);
}

