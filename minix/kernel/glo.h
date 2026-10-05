#ifndef GLO_H
#define GLO_H

/* Global variables used in the kernel. This file contains the declarations;
 * storage space for the variables is allocated in table.c, because EXTERN is
 * defined as extern unless the _TABLE definition is seen. We rely on the 
 * compiler's default initialization (0) for several global variables. 
 */
#ifdef _TABLE
#undef EXTERN
#define EXTERN
#endif

#include <minix/config.h>
#include <minix/ipcconst.h>
#include <machine/archtypes.h>
#include "archconst.h"
#include "config.h"
#include "debug.h"

/* Kernel information structures. This groups vital kernel information. */
extern struct kinfo kinfo;		  /* kernel information for services */
extern struct machine machine;		  /* machine info for services */
extern struct kmessages kmessages;  	  /* diagnostic messages in kernel */
extern struct loadinfo loadinfo;	  /* status of load average */
extern struct kuserinfo kuserinfo;	  /* kernel information for users */
extern struct arm_frclock arm_frclock;	  /* ARM free-running timer info */
extern struct kclockinfo kclockinfo;	  /* clock information */
extern struct minix_kerninfo minix_kerninfo;

EXTERN struct k_randomness krandom; 	/* gather kernel random information */

EXTERN vir_bytes minix_kerninfo_user;

#define kmess kmessages
#define kloadinfo loadinfo

#define system_hz (kclockinfo.hz)		/* HZ value (alias) */

/* Process scheduling information and the kernel reentry count. */
EXTERN struct proc *vmrequest;  /* first process on vmrequest queue */
EXTERN unsigned lost_ticks;	/* clock ticks counted outside clock task */
EXTERN char *ipc_call_names[IPCNO_HIGHEST+1]; /* human-readable call names */
EXTERN struct proc *kbill_kcall; /* process that made kernel call */
EXTERN struct proc *kbill_ipc; /* process that invoked ipc */

/* Interrupt related variables. */
EXTERN irq_hook_t irq_hooks[NR_IRQ_HOOKS];	/* hooks for general use */
EXTERN int irq_actids[NR_IRQ_VECTORS];		/* IRQ ID bits active */
EXTERN int irq_use;				/* map of all in-use irq's */

/* Miscellaneous. */
EXTERN int verboseboot;			/* verbose boot, init'ed in cstart */

#if DEBUG_TRACE
EXTERN int verboseflags;
#endif

#ifdef USE_APIC
EXTERN int config_no_apic; /* optionally turn off apic */
EXTERN int config_apic_timer_x; /* apic timer slowdown factor */
#endif

EXTERN u64_t cpu_hz[CONFIG_MAX_CPUS];

#define cpu_set_freq(cpu, freq)	do {cpu_hz[cpu] = freq;} while (0)
#define cpu_get_freq(cpu)	cpu_hz[cpu]

#ifdef CONFIG_SMP
EXTERN int config_no_smp; /* optionally turn off SMP */
#endif

/* VM */
EXTERN int vm_running;
EXTERN int catch_pagefaults;
EXTERN int kernel_may_alloc;

/* Variables that are initialized elsewhere are just extern here. */
extern struct boot_image image[NR_BOOT_PROCS]; 	/* system image processes */

EXTERN volatile int serial_debug_active;

EXTERN struct cpu_info cpu_info[CONFIG_MAX_CPUS];

/* BKL stats. ReMinix: one cache line per cpu -- every cpu writes its entry
 * on each kernel entry and exit (context_stop()); as four arrays they shared
 * lines among the cpus and with ncpus, cpus[] and others read on every IPI.
 * Dumped as "bkl_stats" (docker/qemu-postmortem.py).
 */
struct bkl_stats {
	u64_t kernel_ticks;	/* cycles in the kernel, BKL held */
	u64_t bkl_ticks;	/* cycles waiting for the BKL */
	unsigned bkl_tries;	/* BKL acquisitions */
	unsigned bkl_succ;	/* ... of them found the lock free */
} __attribute__((aligned(64)));
EXTERN struct bkl_stats bkl_stats[CONFIG_MAX_CPUS];

/* ReMinix: idle polling before hlt, microseconds ("idlepoll", 0 -- off) */
EXTERN unsigned idle_poll_us;
/* ReMinix debug: count IPC pairs ("ipcstat=1", dbg_ipc_count() in proc.c) */
EXTERN int dbg_ipc_stat;

/* Feature flags */
EXTERN int minix_feature_flags;

#endif /* GLO_H */
