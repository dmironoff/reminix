#include <assert.h>

#include "smp.h"
#include "interrupt.h"
#include "clock.h"

unsigned ncpus;
unsigned ht_per_core;
unsigned bsp_cpu_id;

struct cpu cpus[CONFIG_MAX_CPUS];

/* info passed to another cpu along with a sched ipi */
struct sched_ipi_data {
	volatile u32_t	flags;
	volatile u32_t	data;
};

static struct sched_ipi_data  sched_ipi_data[CONFIG_MAX_CPUS];

#define SCHED_IPI_STOP_PROC	1
#define SCHED_IPI_VM_INHIBIT	2
#define SCHED_IPI_SAVE_CTX	4

static volatile unsigned ap_cpus_booted;
/* set once the BSP runs with VM's page tables and the virtual LAPIC address */
static volatile int bsp_paging_enabled;

SPINLOCK_DEFINE(big_kernel_lock)
SPINLOCK_DEFINE(boot_lock)

#ifdef CONFIG_BKL_DEBUG
/*
 * BKL debugging (ReMinix, CONFIG_BKL_DEBUG; see spinlock.h). Plain globals,
 * so that a post-mortem memory dump shows who holds the lock and how it got
 * there:
 *
 *   bkl_owner_cpu, bkl_owner_pc	current holder (cpu -1: free), and the
 *					return address of its bkl_lock() call
 *   bkl_trace[]			ring of the last BKL_TRACE_LEN lock and
 *					unlock events, bkl_trace_seq counts them
 *   bkl_relock_pc, bkl_relock_owner	first time a CPU tried to take the BKL
 *					it already held: the call site and the
 *					site that had taken it
 *
 * Events are recorded while the lock is held (after taking it, before
 * releasing it), so the ring needs no lock of its own.
 */
#define BKL_TRACE_LEN	64
#define BKL_OP_UNLOCK	0
#define BKL_OP_LOCK	1

struct bkl_event {
	u32_t	seq;
	u32_t	pc;		/* return address of bkl_lock/bkl_unlock */
	u32_t	caller;		/* return address of the function that
				 * called BKL_LOCK/UNLOCK (0 if unknown) */
	u16_t	cpu;
	u16_t	op;		/* BKL_OP_* */
};

volatile u32_t bkl_owner_cpu = (u32_t) -1;
volatile u32_t bkl_owner_pc;
volatile u32_t bkl_trace_seq;
volatile u32_t bkl_relock_pc;
volatile u32_t bkl_relock_owner;
struct bkl_event bkl_trace[BKL_TRACE_LEN];

/*
 * Return address one frame further up, through the frame pointer chain (the
 * kernel is built with frame pointers). The frame of the BKL_LOCK caller
 * must lie a little above ours on the same stack; anything else (the entry
 * stubs set %ebp to 0, an AP may start with garbage in it) is not followed.
 */
#define BKL_FRAME_SPAN	8192

static inline u32_t bkl_caller(const u32_t *fp)
{
	const u32_t *up = (const u32_t *) fp[0]; /* frame of the BKL_LOCK caller */

	if (up <= fp || (u32_t) up - (u32_t) fp >= BKL_FRAME_SPAN ||
			((u32_t) up & 3))
		return 0;
	return up[1];
}

static void bkl_record(u32_t pc, u32_t caller, unsigned op)
{
	struct bkl_event *e = &bkl_trace[bkl_trace_seq % BKL_TRACE_LEN];

	e->seq = bkl_trace_seq;
	e->pc = pc;
	e->caller = caller;
	e->cpu = cpuid;
	e->op = op;
	bkl_trace_seq++;
}

__attribute__((noinline)) void bkl_lock(void)
{
	u32_t pc = (u32_t) __builtin_return_address(0);

	if (spinlock_is_locked(&big_kernel_lock)  && bkl_owner_cpu == cpuid && !bkl_relock_pc) {
		/* about to deadlock on ourselves; remember where, and spin
		 * anyway so that the hang can be analysed */
		bkl_relock_owner = bkl_owner_pc;
		bkl_relock_pc = pc;
	}
	spinlock_lock(&big_kernel_lock);
	bkl_owner_cpu = cpuid;
	bkl_owner_pc = pc;
	bkl_record(pc, bkl_caller(__builtin_frame_address(0)), BKL_OP_LOCK);
}

__attribute__((noinline)) void bkl_unlock(void)
{
	u32_t pc = (u32_t) __builtin_return_address(0);

	bkl_record(pc, bkl_caller(__builtin_frame_address(0)), BKL_OP_UNLOCK);
	bkl_owner_cpu = (u32_t) -1;
	bkl_owner_pc = pc;	/* last releaser, while the lock is free */
	spinlock_unlock(&big_kernel_lock);
}
#endif /* CONFIG_BKL_DEBUG */

void wait_for_APs_to_finish_booting(void)
{
	unsigned n = 0;
	int i;

	/* check how many cpus are actually alive */
	for (i = 0 ; i < ncpus ; i++) {
		if (cpu_test_flag(i, CPU_IS_READY))
			n++;
	}
	if (n != ncpus)
		printf("WARNING only %d out of %d cpus booted\n", n, ncpus);

	bsp_paging_enabled = 1;
	barrier();

	/* we must let the other CPUs to run in kernel mode first */
	BKL_UNLOCK();
	while (ap_cpus_booted != (n - 1))
		arch_pause();
	/* now we have to take the lock again as we continue execution */
	BKL_LOCK();
}

/*
 * An AP must not finish booting before the BSP has switched to paging: the
 * BSP releases the BKL whenever it runs a process, so the AP could otherwise
 * take it early, go idle in the boot page tables and, after the BSP moved the
 * LAPIC to its virtual address, send its EOIs to plain memory through those
 * tables -- its LAPIC then blocks all interrupts (docs/testing.md 4.11).
 */
void wait_for_BSP_paging(void)
{
	while (!bsp_paging_enabled)
		arch_pause();
	barrier();
}

void ap_boot_finished(unsigned cpu)
{
	ap_cpus_booted++;
}

void smp_ipi_halt_handler(void)
{
	ipi_ack();
	stop_local_timer();
	arch_smp_halt_cpu();
}

void smp_schedule(unsigned cpu)
{
	arch_send_smp_schedule_ipi(cpu);
}

void smp_sched_handler(void);

/*
 * tell another cpu about a task to do and return only after the cpu acks that
 * the task is finished. Also wait before it finishes task sent by another cpu
 * to the same one.
 */
static void smp_schedule_sync(struct proc * p, unsigned task)
{
	unsigned cpu = p->p_cpu;
	unsigned mycpu = cpuid;

	assert(cpu != mycpu);
	/*
	 * if some other cpu made a request to the same cpu, wait until it is
	 * done before proceeding
	 */
	if (sched_ipi_data[cpu].flags != 0) {
		BKL_UNLOCK();
		while (sched_ipi_data[cpu].flags != 0) {
			if (sched_ipi_data[mycpu].flags) {
				BKL_LOCK();
				smp_sched_handler();
				BKL_UNLOCK();
			}
		}
		BKL_LOCK();
	}

	sched_ipi_data[cpu].data = (u32_t) p;
	sched_ipi_data[cpu].flags |= task;
	__insn_barrier();
	arch_send_smp_schedule_ipi(cpu);

	/* wait until the destination cpu finishes its job */
	BKL_UNLOCK();
	while (sched_ipi_data[cpu].flags != 0) {
		if (sched_ipi_data[mycpu].flags) {
			BKL_LOCK();
			smp_sched_handler();
			BKL_UNLOCK();
		}
	}
	BKL_LOCK();
}

/*
 * Is the process (possibly) executing on its CPU right now? A process that is
 * not runnable any more may still be running there: RTS flags set from
 * another CPU (e.g. RTS_SIGNALED by cause_sig() on behalf of sys_kill) only
 * dequeue it, the CPU running it notices at its next kernel entry. Until
 * then the process keeps running in user mode and its saved context in the
 * process table is stale -- and will be overwritten when it enters the
 * kernel. So stopping such a process must also go through the remote CPU.
 */
static int proc_is_running_remote(struct proc * p)
{
	return p->p_cpu != cpuid && get_cpu_var(p->p_cpu, proc_ptr) == p;
}

void smp_schedule_stop_proc(struct proc * p)
{
	/* ReMinix: also when not runnable but still running, see above. Before,
	 * PM could stop a signaled process and write its signal context (sys_
	 * sigsend) while it was still executing on another CPU: the context
	 * was lost and the process went on making calls PM did not expect
	 * (assert in PM do_sigprocmask, test41). */
	if (proc_is_runnable(p) || proc_is_running_remote(p))
		smp_schedule_sync(p, SCHED_IPI_STOP_PROC);
	else
		RTS_SET(p, RTS_PROC_STOP);
	assert(RTS_ISSET(p, RTS_PROC_STOP));
}

void smp_schedule_vminhibit(struct proc * p)
{
	if (proc_is_runnable(p) || proc_is_running_remote(p))
		smp_schedule_sync(p, SCHED_IPI_VM_INHIBIT);
	else
		RTS_SET(p, RTS_VMINHIBIT);
	assert(RTS_ISSET(p, RTS_VMINHIBIT));
}

void smp_schedule_stop_proc_save_ctx(struct proc * p)
{
	/*
	 * stop the processes and force the complete context of the process to
	 * be saved (i.e. including FPU state and such)
	 */
	smp_schedule_sync(p, SCHED_IPI_STOP_PROC | SCHED_IPI_SAVE_CTX);
	assert(RTS_ISSET(p, RTS_PROC_STOP));
}

/*
 * Move a process to another cpu (sched_proc(): RS placement, SCHED). ReMinix:
 * the original smp_schedule_migrate_proc() went through the old cpu only for
 * a runnable process; for any other one just p_cpu changed. But a process
 * that is not runnable may still be running there (proc_is_running_remote(),
 * docs/testing.md 4.7) and its FPU context may still be held lazily by the
 * old cpu -- it would then run on the new cpu with a stale FPU state. Now
 * the process is always stopped on its old cpu with its whole context saved
 * first. Only a stop we set ourselves is undone: smp_schedule_sync() drops
 * the BKL, so this is not airtight against an RC_STOP from another cpu in
 * that window -- moves happen when RS (re)starts or edits a service.
 */
void smp_move_proc(struct proc * p, unsigned dest_cpu)
{
	int was_stopped;

	if (p->p_cpu == dest_cpu)
		return;

	was_stopped = RTS_ISSET(p, RTS_PROC_STOP);

	if (p->p_cpu != cpuid) {
		/* stop it there and save its complete context, FPU included */
		smp_schedule_sync(p, SCHED_IPI_STOP_PROC | SCHED_IPI_SAVE_CTX);
	} else {
		/* not running: we are; it may still own this cpu's FPU */
		RTS_SET(p, RTS_PROC_STOP);
		if (proc_used_fpu(p) && get_cpulocal_var(fpu_owner) == p) {
			disable_fpu_exception();
			save_local_fpu(p, FALSE /*retain*/);
			release_fpu(p);
		}
	}
	assert(RTS_ISSET(p, RTS_PROC_STOP));

	/* assign the new cpu and let the process run again */
	p->p_cpu = dest_cpu;
	if (!was_stopped)
		RTS_UNSET(p, RTS_PROC_STOP);
}

void smp_sched_handler(void)
{
	unsigned flgs;
	unsigned cpu = cpuid;

	flgs = sched_ipi_data[cpu].flags;

	if (flgs) {
		struct proc * p;
		p = (struct proc *)sched_ipi_data[cpu].data;

		if (flgs & SCHED_IPI_STOP_PROC) {
			RTS_SET(p, RTS_PROC_STOP);
		}
		if (flgs & SCHED_IPI_SAVE_CTX) {
			/* all context has been saved already, FPU remains */
			if (proc_used_fpu(p) &&
					get_cpulocal_var(fpu_owner) == p) {
				disable_fpu_exception();
				save_local_fpu(p, FALSE /*retain*/);
				/* we're preparing to migrate somewhere else */
				release_fpu(p);
			}
		}
		if (flgs & SCHED_IPI_VM_INHIBIT) {
			RTS_SET(p, RTS_VMINHIBIT);
		}
	}

	__insn_barrier();
	sched_ipi_data[cpu].flags = 0;
}

/*
 * This function gets always called only after smp_sched_handler() has been
 * already called. It only serves the purpose of acknowledging the IPI and
 * preempting the current process if the CPU was not idle.
 */
void smp_ipi_sched_handler(void)
{
	extern unsigned dbg_cpu_events[][5];
	struct proc * curr;

	dbg_cpu_events[cpuid][2]++;
	ipi_ack();

	curr = get_cpulocal_var(proc_ptr);
	if (curr->p_endpoint != IDLE) {
		RTS_SET(curr, RTS_PREEMPTED);
	}
}

