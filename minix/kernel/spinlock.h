/*
 * Макросы спинлоков, BKL объявлен здесь же.
 * Этот набор макросов лишь прослойка для arch/<arch>/arch_spinlock.h
 * Реализация спинлоков полностью вынесена в архитектурный слой.
 * Мы используем алгоритм MCS с отдельным массивом
 */
#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

#include "kernel/kernel.h"

#ifndef CONFIG_SMP

#define SPINLOCK_DEFINE(name)
#define SPINLOCK_DECLARE(name)
#define spinlock_init(sl)

/*
 * Без второго CPU замки не нужны: захват всегда успешен, «занят» — никогда.
 * Макросы со значением раскрываются в константы, чтобы их можно было
 * использовать в выражениях (if (spinlock_try_lock(...))); аргумент не
 * вычисляется -- самих переменных замков в такой сборке нет.
 */
#define spinlock_cpu_is_locked(cpu, sl)	(0)
#define spinlock_cpu_try_lock(cpu, sl)	(1)
#define spinlock_cpu_unlock(cpu, sl)

#define spinlock_lock(sl)
#define spinlock_is_locked(sl)		(0)
#define spinlock_try_lock(sl)		(1)
#define spinlock_unlock(sl)

#else

/* SMP */
#include "arch_spinlock.h"


#define SPINLOCK_DEFINE(name) spinlock_t name;
#define SPINLOCK_DECLARE(name) extern spinlock_t name;

#define spinlock_init(sl) arch_spinlock_init(sl)

#if CONFIG_MAX_CPUS == 1
/*
 * Без второго CPU замки не нужны: захват всегда успешен, «занят» — никогда.
 * Макросы со значением раскрываются в константы, чтобы их можно было
 * использовать в выражениях (if (spinlock_try_lock(...))); аргумент не
 * вычисляется -- самих переменных замков в такой сборке нет.
 */
#define spinlock_cpu_is_locked(cpu, sl)	(0)
#define spinlock_cpu_try_lock(cpu, sl)	(1)
#define spinlock_cpu_unlock(cpu, sl)

#define spinlock_lock(sl)
#define spinlock_is_locked(sl)		(0)
#define spinlock_try_lock(sl)		(1)
#define spinlock_unlock(sl)
#else

#define spinlock_cpu_is_locked(cpu, sl) arch_spinlock_cpu_is_locked(sl, cpu)
#define spinlock_cpu_try_lock(cpu, sl) arch_spinlock_try_lock(sl, cpu)
#define spinlock_cpu_unlock(cpu, sl) arch_spinlock_unlock(sl, cpu)

#define spinlock_lock(sl)  arch_spinlock_lock(sl, cpuid)
#define spinlock_is_locked(sl)  arch_spinlock_is_locked(sl)
#define spinlock_try_lock(sl) arch_spinlock_try_lock(sl, cpuid)
#define spinlock_unlock(sl) arch_spinlock_unlock(sl, cpuid)

#endif


#endif /* CONFIG_SMP */

#if defined(CONFIG_SMP) && CONFIG_MAX_CPUS > 1 && defined(CONFIG_BKL_DEBUG)
/*
 * ReMinix, debugging only (CONFIG_BKL_DEBUG, docker/build.mk BKL_DEBUG=yes):
 * the BKL goes through bkl_lock()/bkl_unlock() (smp.c), which record the
 * owner and the last lock/unlock events for post-mortem analysis
 * (docker/qemu-postmortem.py, docs/testing.md) and detect a CPU re-locking
 * the BKL it already holds. They cost a call and shared writes on every
 * kernel entry and exit, so they are off by default.
 */
void bkl_lock(void);
void bkl_unlock(void);
#define BKL_LOCK()	bkl_lock()
#define BKL_UNLOCK()	bkl_unlock()
#else
#define BKL_LOCK()	spinlock_lock(&big_kernel_lock)
#define BKL_UNLOCK()	spinlock_unlock(&big_kernel_lock)
#endif

#endif /* __SPINLOCK_H__ */
