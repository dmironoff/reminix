//
// Created by dmironov on 02.04.2026.
//

#include "resmp.h"
#include "arch_proto.h"
#include "arch_proc_context.h"

static phys_bytes trampoline_addr;

extern mmap_t *system_mmap;
extern void bsp_finish_booting(void);

static volatile tlb_shoot_info_t tlb_shoot_info;
static SPINLOCK(tlb_shoot);
static volatile ipi_call_info_t ipi_call_info[CONFIG_MAX_CPUS];
static SPINLOCK(ipi_call);
static volatile ipi_stop_proc_t ipi_stop_proc_info[CONFIG_MAX_CPUS];
static SPINLOCK(stop_proc);
static volatile ipi_save_ctx_t ipi_save_ctx_info[CONFIG_MAX_CPUS];
static SPINLOCK(save_ctx);
static volatile ipi_vm_inhibit_t ipi_vm_inhibit_info[CONFIG_MAX_CPUS];
static SPINLOCK(vm_inhibit);

struct cpu cpus[CONFIG_MAX_CPUS];

SPINLOCK(big_kernel_lock);
SPINLOCK(boot_lock);

static int booted_cpu = 0;

static irq_hook_t ipi_tlb_shoot_hook;
static irq_hook_t ipi_stop_hook;
static irq_hook_t ipi_reschedule_hook;
static irq_hook_t ipi_call_hook;
static irq_hook_t ipi_stop_proc_hook;
static irq_hook_t ipi_save_ctx_hook;
static irq_hook_t ipi_vm_inhibit_hook;

static void smp_ipi_handle_stop_proc (void) {
    spinlock_lock(stop_proc);
    RTS_SET(ipi_stop_proc_info[cpunr].proc, RTS_PROC_STOP);
    ipi_stop_proc_info[cpunr].result = 1;
    arch_barrier();
    spinlock_unlock(stop_proc);
}

static void smp_ipi_handle_call (void) {
    // так то этот IPI нужен для бесконечной возможности расширения этого интерфейса
    spinlock_lock(ipi_call);
    //swicth (ipi_call_info[cpunr].func) {
    //    default:
    //        break;
   // }
    ipi_call_info[cpunr].result = 1;
    arch_barrier();
    spinlock_unlock(ipi_call);
}

static void smp_ipi_handle_save_ctx(void) {
    spinlock_lock(save_ctx);
    struct proc *p = ipi_save_ctx_info[cpunr].proc;
    if (proc_used_fpu(p) &&
        get_cpulocal_var(fpu_owner) == p) {
        disable_fpu_exception();
        save_local_fpu(p, FALSE /*retain*/);
        /* we're preparing to migrate somewhere else */
        release_fpu(p);
    }
    RTS_SET(p, RTS_PROC_STOP);
    ipi_save_ctx_info[cpunr].result = 1;
    arch_barrier();
    spinlock_unlock(save_ctx);
}

static void smp_ipi_handle_tlb_shoot(void) {
    spinlock_lock(tlb_shoot);
    struct proc *p = tlb_shoot_info.proc;
    arch_proc_tlbi_local(p->context_id);
    tlb_shoot_info.ack_count++;
    spinlock_unlock(tlb_shoot);
}

static void smp_ipi_handle_vm_inhibit(void) {
    spinlock_lock(vm_inhibit);
    RTS_SET(ipi_stop_proc_info[cpunr].proc, RTS_VMINHIBIT);
    ipi_vm_inhibit_info[cpunr].result = 1;
    arch_barrier();
    spinlock_unlock(vm_inhibit);
}

int smp_ipi_irq_handler(struct irq_hook *hook) {
    IPI_ACK();
    if (hook->irq == IPI_NR(IPI_TLB_SHOOT)) {
        smp_ipi_handle_tlb_shoot();
    } else if (hook->irq == IPI_NR(IPI_STOP)) {
        // Остановка CPU из-за паники на другом
        // Функция предполагает остановить выполнение инструкций, отключить прерывания
        // Но оставить все стеки и регистры как есть, что бы можно было отлаживать внешним отладчиком
        arch_stop_cpu();
    } else if (hook->irq == IPI_NR(IPI_RESCHEDULE)) {
        get_cpulocal_var(need_reschedule) = 1; // Просто выставим флаг необходимости перепланирования процессов
        // дальше это задача планировщика, в следующий раз когда он будет занят чем-то
    } else if (hook->irq == IPI_NR(IPI_CALL)) {
        smp_ipi_handle_call();
    } else if (hook->irq == IPI_NR(IPI_STOP_PROC)) {
        smp_ipi_handle_stop_proc();
    } else if (hook->irq == IPI_NR(IPI_SAVE_CTX)) {
        smp_ipi_handle_save_ctx();
    } else if (hook->irq == IPI_NR(IPI_VM_INHIBIT)) {
        smp_ipi_handle_vm_inhibit();
    } else {
        // Какое-то странное прерывание
        return 0;
    }
    return 1;
}

void smp_tss_init_all(void) {
    for(int cpu = 0; cpu < cpu_count ; cpu++)
        if (cpu != bsp_cpu_nr) tss_init(cpu, get_k_stack_top(cpu)) ;
}

void smp_ap_boot(int cpu_nr) {
    arch_smp_boot_cpu(cpu_nr, trampoline_addr);
}

void smp_init(void) {
    if (cpu_count == 1) {
        is_smp_mode = 0;
        return;
    }

    smp_tss_init_all();
    bsp_cpu_nr = cpunr;
    cpu_set_flag(bsp_cpu_nr, CPU_IS_BSP);
    booted_cpu++;

    spinlock_init(big_kernel_lock);
    spinlock_init(boot_lock);
    spinlock_init(tlb_shoot);
    spinlock_init(ipi_call);
    spinlock_init(stop_proc);
    spinlock_init(save_ctx);
    spinlock_init(vm_inhibit);

    arch_smp_init(); // инициализируем архитектуру
    trampoline_addr = arch_smp_copy_trampoline(system_mmap);
    smp_tss_init_all();

    // Регистрируем прерываний для IPI
    // И включаем их обработку в системе
    // Номера прерываний находятся в arch_ipi.h
    put_irq_handler(&ipi_tlb_shoot_hook, IPI_NR(IPI_TLB_SHOOT), smp_ipi_irq_handler);
    enable_irq(&ipi_tlb_shoot_hook);
    put_irq_handler(&ipi_stop_hook, IPI_NR(IPI_STOP), smp_ipi_irq_handler);
    enable_irq(&ipi_stop_hook);
    put_irq_handler(&ipi_reschedule_hook, IPI_NR(IPI_RESCHEDULE), smp_ipi_irq_handler);
    enable_irq(&ipi_reschedule_hook);
    put_irq_handler(&ipi_call_hook, IPI_NR(IPI_CALL), smp_ipi_irq_handler);
    enable_irq(&ipi_call_hook);
    put_irq_handler(&ipi_stop_proc_hook, IPI_NR(IPI_STOP_PROC), smp_ipi_irq_handler);
    enable_irq(&ipi_stop_proc_hook);
    put_irq_handler(&ipi_save_ctx_hook, IPI_NR(IPI_SAVE_CTX), smp_ipi_irq_handler);
    enable_irq(&ipi_save_ctx_hook);
    put_irq_handler(&ipi_vm_inhibit_hook, IPI_NR(IPI_VM_INHIBIT), smp_ipi_irq_handler);
    enable_irq(&ipi_vm_inhibit_hook);

    for (int i = 0; i < cpu_count; i++) {
        if (bsp_cpu_nr != i) {
            smp_ap_boot(i);
        }
    }

    // Подождём пока мы все запустимся
    while (1) {
        spinlock_lock(boot_lock);
        if (booted_cpu == cpu_count) break;
        spinlock_unlock(boot_lock);
    }

    bsp_finish_booting(); // Заканчиваем запуск системы на основном ядре
    NOT_REACHABLE;
}


void smp_ap_init(void) {
    spinlock_lock(boot_lock);
    int cpu = cpunr;

    cpu_identify();
    get_cpulocal_var(bill_ptr) = get_cpulocal_var_ptr(idle_proc);
    get_cpulocal_var(proc_ptr) = get_cpulocal_var_ptr(idle_proc);

    cpu_set_flag(cpu, CPU_IS_READY);
    booted_cpu++;
    spinlock_unlock(boot_lock);

    arm_irq_enable(); // включаем прерывашки на ядре)))))) и полетели =)

    printf("CORE %d READY", cpunr);
    switch_to_user();
    NOT_REACHABLE;
}

/*
 * мы гасим все дополнительные ядра
 * кроме себя.
 */
void smp_shutdown_aps(void) {
    for (int i = 0; i < cpu_count; i++) {
        if (i != cpunr) {
            arch_smp_halt_cpu(i);
        }
    }
}


/*
 * Вызовы для других частей системы, что бы выполнять нужные действия с нашими всеми ядрами
 * Весь прикол, что они проверяют многоядерность системы и мы их можем не опасаясь вызывать из любого места ядра
 */

/*
 * Сбросить кеш контекста процесса у всех ядер
 */
void proc_context_shoot_all(struct proc *p) {
    if (!is_smp_mode) {
        arch_proc_tlbi_local(p->context_id);
        return;
    }

    spinlock_lock(tlb_shoot);
    tlb_shoot_info.proc = p;
    tlb_shoot_info.ack_count = 1;
    arch_proc_tlbi_is(p->context_id);
    spinlock_unlock(tlb_shoot);
}

/*
 * Отправить вызов дополнительной функции на ядро
 */
void ipi_send_call(int cpu_nr, int func, void *data) {
    if (!is_smp_mode) return;
    spinlock_lock(ipi_call);
    ipi_call_info[cpu_nr].func = func;
    ipi_call_info[cpu_nr].data = data;
    ipi_call_info[cpu_nr].result = 0;
    spinlock_unlock(ipi_call);
    arch_send_ipi(cpu_nr, IPI_NR(IPI_CALL));
}

/*
 * Отправить вызов дополнительной функции всем
 */
void ipi_send_call_all(int func, void *data) {
    if (!is_smp_mode) return;
    for (int i = 0; i < cpu_count; i++) {
        if (i != cpunr) {
            spinlock_lock(ipi_call);
            ipi_call_info[i].func = func;
            ipi_call_info[i].data = data;
            ipi_call_info[i].result = 0;
            spinlock_unlock(ipi_call);
        }
    }
    arch_send_ipi_all_others(IPI_NR(IPI_CALL));
}

/*
 * Отправить всем ядрам команду на переход в остановленное состояние для паники
 */
void ipi_send_stop(void) {
    if (!is_smp_mode) return;
    arch_send_ipi_all_others(IPI_NR(IPI_STOP));
}

/*
 * Пересчитать очередь на cpu
 */
void ipi_send_reschedule(int cpu_nr) {
    if (!is_smp_mode) {
        get_cpulocal_var(need_reschedule) = 1;
        return;
    }
    arch_send_ipi(cpu_nr, IPI_NR(IPI_RESCHEDULE));
}

/*
 * Пересчитать очередь всем
 */
void ipi_send_reschedule_all(void) {
    if (!is_smp_mode) {
        get_cpulocal_var(need_reschedule) = 1;
        return;
    }
    arch_send_ipi_all_others(IPI_NR(IPI_RESCHEDULE));
}

/*
 * Остановить выполнение процесса
 */
void ipi_send_stop_proc(struct proc *p) {
    if (!is_smp_mode) {
        RTS_SET(p, RTS_PROC_STOP);
        return;
    }
    if (proc_is_runnable(p)) {
        spinlock_lock(stop_proc);
        ipi_stop_proc_info[p->p_cpu].proc = p;
        ipi_stop_proc_info[p->p_cpu].result = 0;
        spinlock_unlock(stop_proc);
        arch_send_ipi(p->p_cpu, IPI_NR(IPI_STOP_PROC));
    } else {
        RTS_SET(p, RTS_PROC_STOP);
    }
}

/*
 * Остановить процесс и полностью сохранить контекс
 */
void ipi_send_save_ctx(struct proc *p) {
    if (!is_smp_mode) {
        if (proc_used_fpu(p) &&
            get_cpulocal_var(fpu_owner) == p) {
            disable_fpu_exception();
            save_local_fpu(p, FALSE /*retain*/);
            /* we're preparing to migrate somewhere else */
            release_fpu(p);
        }
        RTS_SET(p, RTS_PROC_STOP);
        return;
    }
    spinlock_lock(save_ctx);
    ipi_save_ctx_info[p->p_cpu].proc = p;
    ipi_save_ctx_info[p->p_cpu].result = 0;
    spinlock_unlock(save_ctx);
    arch_send_ipi(p->p_cpu, IPI_NR(IPI_SAVE_CTX));
}

/*
 * Остановить процесс пока VM не разберётся с его таблицей страниц
 * Используется когда требуется переразметить пространство процесса
 */
void ipi_send_vm_inhibit(struct proc *p) {
    if (!is_smp_mode) {
        RTS_SET(p, RTS_VMINHIBIT);
        return;
    }
    if (proc_is_runnable(p)) {
        spinlock_lock(vm_inhibit);
        ipi_vm_inhibit_info[p->p_cpu].proc = p;
        ipi_vm_inhibit_info[p->p_cpu].result = 0;
        spinlock_unlock(vm_inhibit);
        arch_send_ipi(p->p_cpu, IPI_NR(IPI_VM_INHIBIT));
    } else {
        RTS_SET(p, RTS_VMINHIBIT);
    }
}
