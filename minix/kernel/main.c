/* This file contains the main program of MINIX as well as its shutdown code.
 * The routine main() initializes the system and starts the ball rolling by
 * setting up the process table, interrupt vectors, and scheduling each task 
 * to run to initialize itself.
 * The routine shutdown() does the opposite and brings down MINIX. 
 *
 * The entries into this file are:
 *   main:	    	MINIX main program
 *   prepare_shutdown:	prepare to take MINIX down
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
#include "kernel/reup.h"
#include "spinlock.h"

#ifdef USE_WATCHDOG
#include "watchdog.h"
#endif


/* dummy for linking */
char *** _penviron;

/* Prototype declarations for PRIVATE functions. */
static void announce(void);

// Глобальный флаг и переменная с количеством процессоров, которые мы кучи раз будем проверять
int is_smp_mode = 0; // 0 - Uniprocessor mode, 1 - SMP
int cpu_count = 1; // По умолчанию у нас всего один cpu
int bsp_cpu_nr = 0; // Номер процессора на котором мы запустились

//Глобальная переменная с указателем на рабочую карту памяти
mmap_t *system_mmap;

//Глобальная переменная с указателем на рабочую версию абстрактных страниц памяти
vm_abstract_pagetables_t *system_apt;

#ifdef __arm__
    vir_bytes                      fdt_addr;
    arm_pt_t                        *kernel_pt; // Виртуальный адрес отдельной таблицы страниц ядра для регистра ttbr1
#endif

// Основная архитектурно зависимая структура - это таблица физических страниц памяти
// Мы будем её передавать во всякие функции перевода из абстрактной таблицы в физическую
vir_bytes  user_pt_base;

void bsp_finish_booting(void)
{
    int i;
#if SPROFILE
    sprofiling = 0;      /* we're not profiling until instructed to */
#endif /* SPROFILE */

    cpu_identify();

    vm_running = 0;
    krandom.random_sources = RANDOM_SOURCES;
    krandom.random_elements = RANDOM_ELEMENTS;

    /* MINIX is now ready. All boot image processes are on the ready queue.
     * Return to the assembly code to start running the current process.
     */

    /* it should point somewhere */
    get_cpulocal_var(bill_ptr) = get_cpulocal_var_ptr(idle_proc);
    get_cpulocal_var(proc_ptr) = get_cpulocal_var_ptr(idle_proc);
    announce();				/* print MINIX startup banner */

    /*
     * we have access to the cpu local run queue, only now schedule the processes.
     * We ignore the slots for the former kernel tasks
     */
    for (i=0; i < NR_BOOT_PROCS - NR_TASKS; i++) {
        RTS_UNSET(proc_addr(i), RTS_PROC_STOP);
    }
    /*
     * Enable timer interrupts and clock task on the boot CPU.  First reset the
     * CPU accounting values, as the timer initialization (indirectly) uses them.
     */
    cycles_accounting_init();

    if (boot_cpu_init_timer(system_hz)) {
        panic("FATAL : failed to initialize timer interrupts, "
              "cannot continue without any clock source!");
    }

    fpu_init();

/* Warnings for sanity checks that take time. These warnings are printed
 * so it's a clear warning no full release should be done with them
 * enabled.
 */
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
 *			kmain 	                             		*
 *===========================================================================*/
void kmain(bootstrap_kernel_information_t *bki)
{
/* Start the ball rolling. */
  struct boot_image *ip;	/* boot image pointer */
  register struct proc *rp;	/* process pointer */
  register int i, j;
  static int bss_test;
    kinfo_t *local_cbi;

  /* bss sanity check */
  assert(bss_test == 0);
  bss_test = 1;

  /* save a global copy of the boot parameters */
  memcpy(&kinfo, local_cbi, sizeof(kinfo));
  memcpy(&kmess, kinfo.kmess, sizeof(kmess));

   /* We have done this exercise in pre_init so we expect this code
      to simply work! */
   machine.board_id = get_board_id_by_name(env_get(BOARDVARNAME));
#ifdef __arm__
  /* We want to initialize serial before we do any output */
  arch_ser_init();
#endif
  /* We can talk now */
  DEBUGBASIC(("MINIX booting\n"));

  printf("We are in kernel\n");

  /* Kernel may use bits of main memory before VM is started */
  kernel_may_alloc = 1;

  assert(sizeof(kinfo.boot_procs) == sizeof(image));
  memcpy(kinfo.boot_procs, image, sizeof(kinfo.boot_procs));
  cstart();
  BKL_LOCK();
 
   DEBUGEXTRA(("main()\n"));

  /* Clear the process table. Anounce each slot as empty and set up mappings
   * for proc_addr() and proc_nr() macros. Do the same for the table with
   * privilege structures for the system processes and the ipc filter pool.
   */
  proc_init();
  IPCF_POOL_INIT();

   if(NR_BOOT_MODULES != kinfo.mbi.mi_mods_count)
   	panic("expecting %d boot processes/modules, found %d",
		NR_BOOT_MODULES, kinfo.mbi.mi_mods_count);

  /* Set up proc table entries for processes in boot image. */
  for (i=0; i < NR_BOOT_PROCS; ++i) {
	int schedulable_proc;
	proc_nr_t proc_nr;
	int ipc_to_m, kcalls;
	sys_map_t map;

	ip = &image[i];				/* process' attributes */
	DEBUGEXTRA(("initializing %s... ", ip->proc_name));
	rp = proc_addr(ip->proc_nr);		/* get process pointer */
	ip->endpoint = rp->p_endpoint;		/* ipc endpoint */
	rp->p_cpu_time_left = 0;
	if(i < NR_TASKS)			/* name (tasks only) */
		strlcpy(rp->p_name, ip->proc_name, sizeof(rp->p_name));

	if(i >= NR_TASKS) {
		/* Remember this so it can be passed to VM */
		multiboot_module_t *mb_mod = &kinfo.module_list[i - NR_TASKS];
		ip->start_addr = mb_mod->mod_start;
		ip->len = mb_mod->mod_end - mb_mod->mod_start;
	}
	
	reset_proc_accounting(rp);

	/* See if this process is immediately schedulable.
	 * In that case, set its privileges now and allow it to run.
	 * Only kernel tasks and the root system process get to run immediately.
	 * All the other system processes are inhibited from running by the
	 * RTS_NO_PRIV flag. They can only be scheduled once the root system
	 * process has set their privileges.
	 */
	proc_nr = proc_nr(rp);
	schedulable_proc = (iskerneln(proc_nr) || isrootsysn(proc_nr) ||
		proc_nr == VM_PROC_NR);
	if(schedulable_proc) {
        /* Assign privilege structure. Force a static privilege id. */
        (void) get_priv(rp, static_priv_id(proc_nr));

        /* Privileges for kernel tasks. */
        if (proc_nr == VM_PROC_NR) {
            priv(rp)->s_flags = VM_F;
            priv(rp)->s_trap_mask = SRV_T;
            ipc_to_m = SRV_M;
            kcalls = SRV_KC;
            priv(rp)->s_sig_mgr = SELF;
            rp->p_priority = SRV_Q;
            rp->p_quantum_size_ms = SRV_QT;
            rp->context_id = {.generation = 1, id = ARCH_PROC_CONTEXT_VM_ID, };
        } else if (iskerneln(proc_nr)) {
            /* Privilege flags. */
            priv(rp)->s_flags = (proc_nr == IDLE ? IDL_F : TSK_F);
            /* Init flags. */
            priv(rp)->s_init_flags = TSK_I;
            /* Allowed traps. */
            priv(rp)->s_trap_mask = (proc_nr == CLOCK
                                     || proc_nr == SYSTEM ? CSK_T : TSK_T);
            ipc_to_m = TSK_M;                  /* allowed targets */
            kcalls = TSK_KC;                   /* allowed kernel calls */
        } else {
            /* Privileges for the root system process. */
            assert(isrootsysn(proc_nr));
            priv(rp)->s_flags = RSYS_F;        /* privilege flags */
            priv(rp)->s_init_flags = SRV_I;   /* init flags */
            priv(rp)->s_trap_mask = SRV_T;     /* allowed traps */
            ipc_to_m = SRV_M;                 /* allowed targets */
            kcalls = SRV_KC;                  /* allowed kernel calls */
            priv(rp)->s_sig_mgr = SRV_SM;     /* signal manager */
            rp->p_priority = SRV_Q;              /* priority queue */
            rp->p_quantum_size_ms = SRV_QT;   /* quantum size */
        }

        /* Fill in target mask. */
        memset(&map, 0, sizeof(map));

        if (ipc_to_m == ALL_M) {
            for (j = 0; j < NR_SYS_PROCS; j++)
                set_sys_bit(map, j);
        }

        fill_sendto_mask(rp, &map);

        /* Fill in kernel call mask. */
        for (j = 0; j < SYS_CALL_MASK_SIZE; j++) {
            priv(rp)->s_k_call_mask[j] = (kcalls == NO_C ? 0 : (~0));
        }

    } else {
	    /* Don't let the process run for now. */
            RTS_SET(rp, RTS_NO_PRIV | RTS_NO_QUANTUM);
	}

	/* Arch-specific state initialization. */
	arch_boot_proc(ip, rp);

	/* scheduling functions depend on proc_ptr pointing somewhere. */
	if(!get_cpulocal_var(proc_ptr))
        get_cpulocal_var(proc_ptr) = rp;


	/* Process isn't scheduled until VM has set up a pagetable for it. */
	if(rp->p_nr != VM_PROC_NR && rp->p_nr >= 0) {
		rp->p_rts_flags |= RTS_VMINHIBIT;
		rp->p_rts_flags |= RTS_BOOTINHIBIT;
	}

	rp->p_rts_flags |= RTS_PROC_STOP;
	rp->p_rts_flags &= ~RTS_SLOT_FREE;
	DEBUGEXTRA(("done\n"));
  }

  /* update boot procs info for VM */
  memcpy(kinfo.boot_procs, image, sizeof(kinfo.boot_procs));

#define IPCNAME(n) { \
	assert((n) >= 0 && (n) <= IPCNO_HIGHEST); \
	assert(!ipc_call_names[n]);	\
	ipc_call_names[n] = #n; \
}

  arch_post_init();

  IPCNAME(SEND);
  IPCNAME(RECEIVE);
  IPCNAME(SENDREC);
  IPCNAME(NOTIFY);
  IPCNAME(SENDNB);
  IPCNAME(SENDA);

  /* System and processes initialization */
  memory_init();

  DEBUGEXTRA(("system_init()... "));
  system_init();

  DEBUGEXTRA(("done\n"));

  /* The bootstrap phase is over, so we can add the physical
   * memory used for it to the free list.
   */
  // TODO: Сделать очистку области bootstrap в перенесённом ядре
  //add_memmap(&kinfo, kinfo.bootstrap_start, kinfo.bootstrap_len);

  if (is_smp_mode) {
      smp_init();
      // Вообще из этой функции не выходят, но если это случилось, то значит у нас провалился запуск SMP
      printf("SMP INIT FAILED - SWITCHING TO UNIPROCESSOR MODE\r\n");
      cpu_count = 1;
      is_smp_mode = 0;
      up_finish_booting();
  } else {
      // Сразу запускаемся в режиме без SMP
      up_finish_booting();
  }

  NOT_REACHABLE;
}

/*===========================================================================*
 *				announce				     *
 *===========================================================================*/
void announce(void)
{
  /* Display the MINIX startup banner. */
  printf("\nMINIX %s. "
#ifdef PAE
"(PAE) "
#endif
#ifdef _VCS_REVISION
	"(" _VCS_REVISION ")\n"
#endif
      "Copyright 2016, Vrije Universiteit, Amsterdam, The Netherlands\n",
      OS_RELEASE);
  printf("MINIX is open source software, see http://www.minix3.org\n");
}

/*===========================================================================*
 *				prepare_shutdown			     *
 *===========================================================================*/
void prepare_shutdown(const int how)
{
/* This function prepares to shutdown MINIX. */
  static minix_timer_t shutdown_timer;

  /* Continue after 1 second, to give processes a chance to get scheduled to 
   * do shutdown work.  Set a watchog timer to call shutdown(). The timer 
   * argument passes the shutdown status. 
   */
  printf("MINIX will now be shut down ...\n");
  set_kernel_timer(&shutdown_timer, get_monotonic() + system_hz,
      minix_shutdown, how);
}

/*===========================================================================*
 *				shutdown 				     *
 *===========================================================================*/
void minix_shutdown(int how)
{
/* This function is called from prepare_shutdown or stop_sequence to bring 
 * down MINIX.
 */

  /* 
   * FIXME
   *
   * we will need to stop timers on all cpus if SMP is enabled and put them in
   * such a state that we can perform the whole boot process once restarted from
   * monitor again
   */
  if (is_smp_mode)
	  smp_shutdown_aps();

  hw_intr_disable_all();
  stop_local_timer();

  /* Show shutdown message */
  direct_cls();
  if((how & RB_POWERDOWN) == RB_POWERDOWN)
	direct_print("MINIX has halted and will now power off.\n");
  else if(how & RB_HALT)
	direct_print("MINIX has halted. "
		     "It is safe to turn off your computer.\n");
  else
	direct_print("MINIX will now reset.\n");
  arch_shutdown(how);
}

/*===========================================================================*
 *				cstart					     *
 *===========================================================================*/
void cstart(void)
{
/* Perform system initializations prior to calling main(). Most settings are
 * determined with help of the environment strings passed by MINIX' loader.
 */
  register char *value;				/* value in key=value pair */


    /* low-level initialization */
  prot_init();


  /* determine verbosity */
  if ((value = env_get(VERBOSEBOOTVARNAME)))
	  verboseboot = atoi(value);

  /* Initialize clock variables. */
  init_clock();


  /* Get memory parameters. */
  value = env_get("ac_layout");
  if(value && atoi(value)) {
        kinfo.user_sp = (vir_bytes) USR_STACKTOP_COMPACT;
        kinfo.user_end = (vir_bytes) USR_DATATOP_COMPACT;
  }

  DEBUGEXTRA(("cstart\n"));

  /* Record miscellaneous information for user-space servers. */
  kinfo.nr_procs = NR_PROCS;
  kinfo.nr_tasks = NR_TASKS;
  strlcpy(kinfo.release, OS_RELEASE, sizeof(kinfo.release));
  strlcpy(kinfo.version, OS_VERSION, sizeof(kinfo.version));

  /* Initialize various user-mapped structures. */
  memset(&arm_frclock, 0, sizeof(arm_frclock));

  memset(&kuserinfo, 0, sizeof(kuserinfo));
  kuserinfo.kui_size = sizeof(kuserinfo);
  kuserinfo.kui_user_sp = kinfo.user_sp;

#ifdef USE_APIC
  value = env_get("no_apic");
  if(value)
	config_no_apic = atoi(value);
  else
	config_no_apic = 1;
  value = env_get("apic_timer_x");
  if(value)
	config_apic_timer_x = atoi(value);
  else
	config_apic_timer_x = 1;
#endif

#ifdef USE_WATCHDOG
  value = env_get("watchdog");
  if (value)
	  watchdog_enabled = atoi(value);
#endif

// TODO: Вынести эту хуиту с apic в код для 86
  if (config_no_apic)
	  is_smp_mode = 0;

  value = env_get("no_smp");
  if(value)
	is_smp_mode = ~atoi(value);

  DEBUGEXTRA(("intr_init(0)\n"));

  intr_init(0);


  arch_init();

}

/*===========================================================================*
 *				env_get				     	*
 *===========================================================================*/
char *env_get(const char *name)
{
    return params_get_value(kinfo.params, name);
    return "0";
}

void cpu_print_freq(unsigned cpu)
{
        u64_t freq;

        freq = cpu_get_freq(cpu);
        DEBUGBASIC(("CPU %d freq %lu MHz\n", cpu, (unsigned long)(freq / 1000000)));
}

int is_fpu(void)
{
        return get_cpulocal_var(fpu_presence);
}

