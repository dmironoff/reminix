#ifndef PROC_H
#define PROC_H

#include <minix/const.h>
#include <sys/cdefs.h>

#ifndef __ASSEMBLY__

#include <minix/com.h>
#include <minix/portio.h>
#include "const.h"
#include "priv.h"
#include "kernel/proc_context.h"
#include "kmutex.h"
#include "minix/abstract_pagetables.h"

/* Sentinel: у процесса нет физической таблицы (ядровые задачи, незапущенные) */
#define PT_HANDLER_NONE  ((uint32_t)0xFFFFFFFFu)

struct proc {
    struct stackframe_s p_reg;	/* process' registers saved in stack frame */
    struct segframe p_seg;	/* segment descriptors (p_ttbr / p_ttbr_v) */
    proc_nr_t p_nr;		/* number of this process (for fast access) */
    struct priv *p_priv;		/* system privileges structure */
    volatile u32_t p_rts_flags;	/* process is runnable only if zero */
    volatile u32_t p_misc_flags;	/* flags that do not suspend the process */

    char p_priority;
    u64_t p_cpu_time_left;
    unsigned p_quantum_size_ms;
    struct proc *p_scheduler;
    unsigned p_cpu;
    bitchunk_t p_cpu_mask[BITMAP_CHUNKS(CONFIG_MAX_CPUS)];
    bitchunk_t p_stale_tlb[BITMAP_CHUNKS(CONFIG_MAX_CPUS)];

    struct {
        u64_t enter_queue;
        u64_t time_in_queue;
        unsigned long dequeues;
        unsigned long ipc_sync;
        unsigned long ipc_async;
        unsigned long preempted;
    } p_accounting;

    clock_t p_dequeued;
    clock_t p_user_time;
    clock_t p_sys_time;
    clock_t p_virt_left;
    clock_t p_prof_left;

    u64_t p_cycles;
    u64_t p_kcall_cycles;
    u64_t p_kipc_cycles;
    u64_t p_tick_cycles;
    struct cpuavg p_cpuavg;

    struct proc *p_nextready;
    struct proc *p_caller_q;
    struct proc *p_q_link;
    endpoint_t p_getfrom_e;
    endpoint_t p_sendto_e;

    sigset_t p_pending;

    char p_name[PROC_NAME_LEN];
    endpoint_t p_endpoint;

    message p_sendmsg;
    message p_delivermsg;
    vir_bytes p_delivermsg_vir;

    struct {
        struct proc	*nextrestart;
        struct proc	*nextrequestor;
#define VMSTYPE_SYS_NONE	0
#define VMSTYPE_KERNELCALL	1
#define VMSTYPE_DELIVERMSG	2
#define VMSTYPE_MAP		3
        int		type;
        union ixfer_saved{
            message		reqmsg;
        } saved;
        int		req_type;
        endpoint_t	target;
        union ixfer_params{
            struct {
                vir_bytes 	start, length;
                u8_t		writeflag;
            } check;
        } params;
        int		vmresult;
    } p_vmrequest;

    int p_found;
    int p_magic;

    struct { reg_t r1, r2, r3; } p_defer;

    /*
     * Виртуальный адрес APT и применённая версия
     */
    vm_abstract_pt_t    *apt_table;
    unsigned long       apt_version; //По ней мы будем смотреть стоит ли применять изменения на лету

    /*
     * Индекс в массиве arm_pt_t (arch_pt_base из BKI).
     * PT_HANDLER_NONE — таблица не назначена (ядровые задачи, init-фаза).
     * Выставляется в arch_boot_proc для VM и в sys_vmctl VMCTL_SETPT для остальных.
     * p_ttbr / p_ttbr_v сохранены для совместимости с mpx.S и assert'ами.
     */
    uint32_t pt_handler;

    /*
     * ASID/PCID кеширование контекста:
     *   ARM/AArch64/RISC-V — ASID
     *   x86-64             — PCID
     *   i586               - Нет, наша архитектурная часть должна это учитывать
     */
    proc_context_id_t context_id;

#if DEBUG_TRACE
    int p_schedules;
#endif
};

#endif /* __ASSEMBLY__ */

/* Bits for p_rts_flags */
#define RTS_SLOT_FREE	0x01
#define RTS_PROC_STOP	0x02
#define RTS_SENDING	0x04
#define RTS_RECEIVING	0x08
#define RTS_SIGNALED	0x10
#define RTS_SIG_PENDING	0x20
#define RTS_P_STOP	0x40
#define RTS_NO_PRIV	0x80
#define RTS_NO_ENDPOINT	0x100
#define RTS_VMINHIBIT	0x200
#define RTS_PAGEFAULT	0x400
#define RTS_VMREQUEST	0x800
#define RTS_VMREQTARGET	0x1000
#define RTS_PREEMPTED	0x4000
#define RTS_NO_QUANTUM	0x8000
#define RTS_BOOTINHIBIT	0x10000

#define rts_f_is_runnable(flg)	((flg) == 0)
#define proc_is_runnable(p)	(rts_f_is_runnable((p)->p_rts_flags))
#define proc_is_preempted(p)	((p)->p_rts_flags & RTS_PREEMPTED)
#define proc_no_quantum(p)	((p)->p_rts_flags & RTS_NO_QUANTUM)
#define proc_ptr_ok(p)		((p)->p_magic == PMAGIC)
#define proc_used_fpu(p)	((p)->p_misc_flags & (MF_FPU_INITIALIZED))
#define proc_kernel_scheduler(p) ((p)->p_scheduler == NULL || \
				  (p)->p_scheduler == (p))

#define P_BLOCKEDON(p) \
	(((p)->p_rts_flags & RTS_SENDING) ? (p)->p_sendto_e : \
	 (((p)->p_rts_flags & RTS_RECEIVING) ? (p)->p_getfrom_e : NONE))

#define RTS_ISSET(rp, f) (((rp)->p_rts_flags & (f)) == (f))

#define RTS_SET(rp, f) \
	do { \
		const int rts = (rp)->p_rts_flags; \
		(rp)->p_rts_flags |= (f); \
		if (rts_f_is_runnable(rts) && !proc_is_runnable(rp)) dequeue(rp); \
	} while(0)

#define RTS_UNSET(rp, f) \
	do { \
		int rts = (rp)->p_rts_flags; \
		(rp)->p_rts_flags &= ~(f); \
		if (!rts_f_is_runnable(rts) && proc_is_runnable(rp)) enqueue(rp); \
	} while(0)

#define RTS_SETFLAGS(rp, f) \
	do { \
		if (proc_is_runnable(rp) && (f)) dequeue(rp); \
		(rp)->p_rts_flags = (f); \
	} while(0)

/* Misc flags */
#define MF_REPLY_PEND		0x001
#define MF_VIRT_TIMER		0x002
#define MF_PROF_TIMER		0x004
#define MF_KCALL_RESUME		0x008
#define MF_DELIVERMSG		0x040
#define MF_SIG_DELAY		0x080
#define MF_SC_ACTIVE		0x100
#define MF_SC_DEFER		0x200
#define MF_SC_TRACE		0x400
#define MF_FPU_INITIALIZED	0x1000
#define MF_SENDING_FROM_KERNEL	0x2000
#define MF_CONTEXT_SET		0x4000
#define MF_SPROF_SEEN		0x8000
#define MF_FLUSH_TLB		0x10000
#define MF_SENDA_VM_MISS	0x20000
#define MF_STEP			0x40000
#define MF_MSGFAILED		0x80000
#define MF_NICED		0x100000

#define BEG_PROC_ADDR (&proc[0])
#define BEG_USER_ADDR (&proc[NR_TASKS])
#define END_PROC_ADDR (&proc[NR_TASKS + NR_PROCS])

#define proc_addr(n)      (&(proc[NR_TASKS + (n)]))
#define proc_nr(p)        ((p)->p_nr)

#define isokprocn(n)  ((unsigned)((n) + NR_TASKS) < NR_PROCS + NR_TASKS)
#define isemptyn(n)   isemptyp(proc_addr(n))
#define isemptyp(p)   ((p)->p_rts_flags == RTS_SLOT_FREE)
#define iskernelp(p)  ((p) < BEG_USER_ADDR)
#define iskerneln(n)  ((n) < 0)
#define isuserp(p)    isusern((p) >= BEG_USER_ADDR)
#define isusern(n)    ((n) >= 0)
#define isrootsysn(n) ((n) == ROOT_SYS_PROC_NR)

#ifndef __ASSEMBLY__
EXTERN struct proc proc[NR_TASKS + NR_PROCS];
int mini_send(struct proc *caller_ptr, endpoint_t dst_e, message *m_ptr, int flags);
#endif

#endif /* PROC_H */
