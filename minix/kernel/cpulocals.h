/* Implementation of CPU local variables generics */
#ifndef __CPULOCALS_H__
#define __CPULOCALS_H__

#ifndef __ASSEMBLY__

#include "resmp.h"

#define get_cpu_var(cpu, name)		__cpu_local_vars[cpu].name
#define get_cpu_var_ptr(cpu, name)	(&(get_cpu_var(cpu, name)))
#define get_cpulocal_var(name)  __cpu_local_vars[cpunr].name
#define get_cpulocal_var_ptr(name)  (&(get_cpu_var(cpunr, name)))

#include "kernel/proc_context.h"


/* FIXME - padd the structure so that items in the array do not share cacheline
 * with other cpus */

/*
 * The global cpu local variables in use
 */
extern struct __cpu_local_vars {

/* Process scheduling information and the kernel reentry count. */
	struct proc *proc_ptr;/* pointer to currently running process */
	struct proc *bill_ptr;/* process to bill for clock ticks */
	struct proc idle_proc;/* stub for an idle process */

/* 
 * signal whether pagefault is already being handled to detect recursive
 * pagefaults
 */
	int pagefault_handled;

/*
 * which processpage tables are loaded right now. We need to know this because
 * some processes are loaded in each process pagetables and don't have their own
 * pagetables. Therefore we cannot use the proc_ptr pointer
 */
	struct proc * ptproc;

/* CPU private run queues */
	struct proc * run_q_head[NR_SCHED_QUEUES]; /* ptrs to ready list headers */
	struct proc * run_q_tail[NR_SCHED_QUEUES]; /* ptrs to ready list tails */
	int cpu_is_idle; /* let the others know that you are idle */

	int idle_interrupted; /* to interrupt busy-idle
						     while profiling */

	u64_t tsc_ctr_switch; /* when did we switched time accounting */

/* last values read from cpu when sending ooq msg to scheduler */
	u64_t cpu_last_tsc;
	u64_t cpu_last_idle;


	char fpu_presence; /* whether the cpu has FPU or not */
	struct proc * fpu_owner; /* who owns the FPU of the local cpu */

    int cpu_is_bsp; // Флаг того что именно с этого процессора произошёл запуск
    int ht_nr; // Поддержка гипертрединга, количество псевдоконвееров на этом ядре

    uint32_t arch_cpu_id; // Архитектурно зависимая идентификация cpu

    volatile int need_reschedule;

    proc_context_id_t context_id; // текущий загруженный в ядро контекст

} __cpu_local_vars [CONFIG_MAX_CPUS];

#endif /* __ASSEMBLY__ */

#endif /* __CPULOCALS_H__ */
