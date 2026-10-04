#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

#include "kernel/kernel.h"

typedef struct spinlock {
	atomic_t val;
} spinlock_t;

#ifndef CONFIG_SMP

#define SPINLOCK_DEFINE(name)
#define PRIVATE_SPINLOCK_DEFINE(name)
#define SPINLOCK_DECLARE(name)
#define spinlock_init(sl)
#define spinlock_lock(sl)
#define spinlock_unlock(sl)

#else

/* SMP */
#define SPINLOCK_DEFINE(name)	spinlock_t name;
#define PRIVATE_SPINLOCK_DEFINE(name)	PRIVATE SPINLOCK_DEFINE(name)
#define SPINLOCK_DECLARE(name)	extern SPINLOCK_DEFINE(name)
#define spinlock_init(sl) do { (sl)->val = 0; } while (0)

#if CONFIG_MAX_CPUS == 1
#define spinlock_lock(sl)
#define spinlock_unlock(sl)
#else
void arch_spinlock_lock(atomic_t * sl);
void arch_spinlock_unlock(atomic_t * sl);
#define spinlock_lock(sl)	arch_spinlock_lock((atomic_t*) sl)
#define spinlock_unlock(sl)	arch_spinlock_unlock((atomic_t*) sl)
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
