/* main.c — ReMinix kernel entry point.
 * kmain() accepts bootstrap_kernel_information_t from pre_init.
 * No multiboot, no kinfo.mbi, no module_list parsing here.
 */

#include "arch_configs.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <minix/endpoint.h>
#include <machine/vmparam.h>
#include <minix/u64.h>
#include <minix/board.h>
#include <sys/reboot.h>
#include "clock.h"
#include "direct_utils.h"
#include "hw_intr.h"
#include "arch_proto.h"
#include "kernel/env_params_utils.h"
#include "kernel/bootstrap_kernel_information.h"
#include "kernel/apt_utils.h"
#include "kernel/mmap_utils.h"
#include "kernel/resmp.h"
#include "spinlock.h"
#include "arch_proc_context.h"

#ifdef USE_WATCHDOG
#include "watchdog.h"
#endif

/* Глобальный указатель на BKI — используется в memory.c и protect.c */
bootstrap_kernel_information_t *BKI;

/* dummy for linking */
char *** _penviron;

static void announce(void);

/* SMP globals */
int is_smp_mode = 0;
int cpu_count   = 1;
int bsp_cpu_nr  = 0;

/* Глобальные указатели на рабочие структуры памяти */
mmap_t                   *system_mmap;
vm_abstract_pagetables_t *system_apt;

#ifdef __arm__
vir_bytes  fdt_addr;
arm_pt_t  *kernel_pt;
#endif

/* Физические таблицы страниц — передаётся во всё архитектурно-зависимое */
vir_bytes user_pt_base;

/* ------------------------------------------------------------------ */

void bsp_finish_booting(void)
{
    int i;
#if SPROFILE
    sprofiling = 0;
#endif
    cpu_identify();

    vm_running = 0;
    krandom.random_sources  = RANDOM_SOURCES;
    krandom.random_elements = RANDOM_ELEMENTS;

    get_cpulocal_var(bill_ptr) = get_cpulocal_var_ptr(idle_proc);
    get_cpulocal_var(proc_ptr) = get_cpulocal_var_ptr(idle_proc);
    announce();

    for (i = 0; i < NR_BOOT_PROCS - NR_TASKS; i++)
        RTS_UNSET(proc_addr(i), RTS_PROC_STOP);

    cycles_accounting_init();

    if (boot_cpu_init_timer(system_hz))
        panic("FATAL: failed to initialize timer interrupts");

    fpu_init();

#if DEBUG_SCHED_CHECK
    FIXME("DEBUG_SCHED_CHECK enabled");
#endif
#if DEBUG_VMASSERT
    FIXME("DEBUG_VMASSERT enabled");
#endif
#if DEBUG_PROC_CHECK
    FIXME("PROC check enabled");
#endif

    cpu_set_flag(bsp_cpu_nr, CPU_IS_READY);
    machine.processors_count = cpu_count;
    machine.bsp_id = bsp_cpu_nr;

    switch_to_user();
    NOT_REACHABLE;
}

/*===========================================================================*
 *                              kmain                                        *
 *===========================================================================*/
void kmain(bootstrap_kernel_information_t *bki)
{
    struct boot_image *ip;
    register struct proc *rp;
    register int i, j;
    static int bss_test;

    /* BSS sanity check */
    assert(bss_test == 0);
    bss_test = 1;

    /* ----- Сохраняем BKI глобально ----- */
    BKI         = bki;
    system_mmap = bki->mmap;
    system_apt  = bki->apt;

#ifdef __arm__
    fdt_addr  = bki->fdt_addr;
    kernel_pt = (arm_pt_t *) bki->arch_pt_base;
    user_pt_base = bki->arch_pt_base;
#endif

    /* Переносим параметры загрузки в kinfo.params для env_get() */
    strlcpy(kinfo.params, bki->params, sizeof(kinfo.params));

    /* Параметры адресного пространства пользователя */
    kinfo.user_sp  = (vir_bytes) USR_STACKTOP;
    kinfo.user_end = (vir_bytes) USR_DATATOP;

    /* Минимально необходимые поля kinfo */
    kinfo.nr_procs = NR_PROCS;
    kinfo.nr_tasks = NR_TASKS;
    strlcpy(kinfo.release, OS_RELEASE, sizeof(kinfo.release));
    strlcpy(kinfo.version, OS_VERSION, sizeof(kinfo.version));

    /* SMP: количество ядер из BKI */
    cpu_count  = (int) bki->system_cpu_count;
    bsp_cpu_nr = (int) bki->boot_cpu_number;
    if (cpu_count > 1)
        is_smp_mode = 1;

    machine.board_id = get_board_id_by_name(env_get(BOARDVARNAME));

#ifdef __arm__
    arch_ser_init();
#endif

    DEBUGBASIC(("ReMinix booting\n"));
    printf("We are in kernel\n");


    cstart();
    BKL_LOCK();

    DEBUGEXTRA(("main()\n"));

    proc_init();
    IPCF_POOL_INIT();

    /* Заполняем таблицу процессов из boot image */
    for (i = 0; i < NR_BOOT_PROCS; ++i) {
        int schedulable_proc;
        proc_nr_t proc_nr;
        int ipc_to_m, kcalls;
        sys_map_t map;

        ip = &image[i];
        DEBUGEXTRA(("initializing %s... ", ip->proc_name));
        rp = proc_addr(ip->proc_nr);
        ip->endpoint = rp->p_endpoint;
        rp->p_cpu_time_left = 0;

        if (i < NR_TASKS)
            strlcpy(rp->p_name, ip->proc_name, sizeof(rp->p_name));

        if (i >= NR_TASKS) {
            /* Ищем соответствующий модуль в BKI->modules[] по имени */
            for (j = 0; j < BOOT_MODULES_MAX_COUNT; j++) {
                if (strcmp(bki->modules[j].name, ip->proc_name) == 0) {
                    ip->start_addr = bki->modules[j].addr;
                    ip->len        = bki->modules[j].size;
                    break;
                }
            }
        }

        reset_proc_accounting(rp);

        proc_nr = proc_nr(rp);
        schedulable_proc = (iskerneln(proc_nr) || isrootsysn(proc_nr) ||
                            proc_nr == VM_PROC_NR);

        if (schedulable_proc) {
            (void) get_priv(rp, static_priv_id(proc_nr));

            if (proc_nr == VM_PROC_NR) {
                priv(rp)->s_flags    = VM_F;
                priv(rp)->s_trap_mask = SRV_T;
                ipc_to_m = SRV_M;
                kcalls   = SRV_KC;
                priv(rp)->s_sig_mgr = SELF;
                rp->p_priority        = SRV_Q;
                rp->p_quantum_size_ms = SRV_QT;
                /* ASID/PCID для VM — фиксированный слот */
                rp->context_id.generation = 1;
                rp->context_id.id         = ARCH_PROC_CONTEXT_VM_ID;
            } else if (iskerneln(proc_nr)) {
                priv(rp)->s_flags      = (proc_nr == IDLE ? IDL_F : TSK_F);
                priv(rp)->s_init_flags = TSK_I;
                priv(rp)->s_trap_mask  = (proc_nr == CLOCK ||
                                          proc_nr == SYSTEM ? CSK_T : TSK_T);
                ipc_to_m = TSK_M;
                kcalls   = TSK_KC;
            } else {
                assert(isrootsysn(proc_nr));
                priv(rp)->s_flags      = RSYS_F;
                priv(rp)->s_init_flags = SRV_I;
                priv(rp)->s_trap_mask  = SRV_T;
                ipc_to_m = SRV_M;
                kcalls   = SRV_KC;
                priv(rp)->s_sig_mgr   = SRV_SM;
                rp->p_priority        = SRV_Q;
                rp->p_quantum_size_ms = SRV_QT;
            }

            memset(&map, 0, sizeof(map));
            if (ipc_to_m == ALL_M)
                for (j = 0; j < NR_SYS_PROCS; j++)
                    set_sys_bit(map, j);
            fill_sendto_mask(rp, &map);

            for (j = 0; j < SYS_CALL_MASK_SIZE; j++)
                priv(rp)->s_k_call_mask[j] = (kcalls == NO_C ? 0 : (~0));
        } else {
            RTS_SET(rp, RTS_NO_PRIV | RTS_NO_QUANTUM);
        }

        arch_boot_proc(ip, rp);

        if (!get_cpulocal_var(proc_ptr))
            get_cpulocal_var(proc_ptr) = rp;

        if (rp->p_nr != VM_PROC_NR && rp->p_nr >= 0) {
            rp->p_rts_flags |= RTS_VMINHIBIT;
            rp->p_rts_flags |= RTS_BOOTINHIBIT;
        }

        rp->p_rts_flags |= RTS_PROC_STOP;
        rp->p_rts_flags &= ~RTS_SLOT_FREE;
        DEBUGEXTRA(("done\n"));
    }

    arch_post_init();

    /* IPC names */
#define IPCNAME(n) do { \
    assert((n) >= 0 && (n) <= IPCNO_HIGHEST); \
    assert(!ipc_call_names[n]); \
    ipc_call_names[n] = #n; \
} while(0)
    IPCNAME(SEND);
    IPCNAME(RECEIVE);
    IPCNAME(SENDREC);
    IPCNAME(NOTIFY);
    IPCNAME(SENDNB);
    IPCNAME(SENDA);

    memory_init();

    DEBUGEXTRA(("system_init()... "));
    system_init();
    DEBUGEXTRA(("done\n"));

    /* TODO: освободить область bootstrap после запуска */
    /* Думаю, что тоже переложим на VM*/
    /* add_memmap(bki->bootstrap_start, bki->bootstrap_len); */

    if (is_smp_mode) {
        smp_init();
        printf("SMP INIT FAILED - SWITCHING TO UNIPROCESSOR MODE\r\n");
        cpu_count  = 1;
        is_smp_mode = 0;
        bsp_finish_booting();
    } else {
        bsp_finish_booting();
    }

    NOT_REACHABLE;
}

/*===========================================================================*
 *                              cstart                                       *
 *===========================================================================*/
void cstart(void)
{
    register char *value;

    prot_init();

    if ((value = env_get(VERBOSEBOOTVARNAME)))
        verboseboot = atoi(value);

    init_clock();

    value = env_get("ac_layout");
    if (value && atoi(value)) {
        kinfo.user_sp  = (vir_bytes) USR_STACKTOP_COMPACT;
        kinfo.user_end = (vir_bytes) USR_DATATOP_COMPACT;
    }

    DEBUGEXTRA(("cstart\n"));

    memset(&arm_frclock, 0, sizeof(arm_frclock));
    memset(&kuserinfo, 0, sizeof(kuserinfo));
    kuserinfo.kui_size   = sizeof(kuserinfo);
    kuserinfo.kui_user_sp = kinfo.user_sp;

    value = env_get("no_smp");
    if (value)
        is_smp_mode = ~atoi(value);

    DEBUGEXTRA(("intr_init(0)\n"));
    intr_init(0);
    arch_init();
}

/*===========================================================================*
 *                              announce                                     *
 *===========================================================================*/
static void announce(void)
{
    printf("\nReMinix %s. "
           #ifdef _VCS_REVISION
           "(" _VCS_REVISION ")\n"
           #endif
           "Copyright 2026, ReMinix Project\n",
           OS_RELEASE);
}

/*===========================================================================*
 *                          prepare_shutdown                                 *
 *===========================================================================*/
void prepare_shutdown(const int how)
{
    static minix_timer_t shutdown_timer;
    printf("MINIX will now be shut down ...\n");
    set_kernel_timer(&shutdown_timer, get_monotonic() + system_hz,
                     minix_shutdown, how);
}

/*===========================================================================*
 *                          minix_shutdown                                   *
 *===========================================================================*/
void minix_shutdown(int how)
{
    if (is_smp_mode)
        smp_shutdown_aps();

    hw_intr_disable_all();
    stop_local_timer();

    direct_cls();
    if ((how & RB_POWERDOWN) == RB_POWERDOWN)
        direct_print("MINIX has halted and will now power off.\n");
    else if (how & RB_HALT)
        direct_print("MINIX has halted. It is safe to turn off your computer.\n");
    else
        direct_print("MINIX will now reset.\n");
    arch_shutdown(how);
}

/*===========================================================================*
 *                              env_get                                      *
 *===========================================================================*/
char *env_get(const char *name)
{
    return params_get_value(kinfo.params, name);
}

void cpu_print_freq(unsigned cpu)
{
    u64_t freq = cpu_get_freq(cpu);
    DEBUGBASIC(("CPU %d freq %lu MHz\n", cpu,
            (unsigned long)(freq / 1000000)));
}

int is_fpu(void)
{
    return get_cpulocal_var(fpu_presence);
}
