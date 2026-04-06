//
// Created by dmironov on 28.03.2026.
//

#ifndef REMINIX_PROC_HARDWARE_CONTEXT_H
#define REMINIX_PROC_HARDWARE_CONTEXT_H

#include "cpufunc.h"
#include "kernel/proc_context.h"
#include "kernel/proc.h"

#define PCONTEXT_ERROR_NO_FREE  -1

#define ARCH_PROC_CONTEXT_COUNT 256
#define ARCH_PROC_CONTEXT_KERNEL_ID 0
#define ARCH_PROC_CONTEXT_VM_ID    1
#define ARCH_PROC_CONTEXT_MIN_ID    2

typedef struct {
    uint32_t generation;
    uint32_t next_id;
} arch_context_allocator_t;

/*
 * Инициализация механизма ASID
 */
int arch_proc_context_init(void);
/*
 * Назначить новый идентификатор контекста процессу
 * Защищена spinlock_irq
 */
void arch_proc_assign_context_id(struct proc *p);
/*
 * Проверка валидности текущего идентификатора
 * Если нет, то она сама поставит новый идентификатор в процесс
 */
void arch_proc_check_context_id(struct proc *p);
/*
 * Универсальная функция для любых систем
 * сбросить весь asid
 */
void arch_proc_context_flush();
/*
 * Универсальная функция для любых систем
 * сбросить asid для идентификатора
 */
void arch_proc_context_id_flush(proc_context_id_t id);
/*
 * Сбросить весь ASID TLB на локальном процессоре
 */
void arch_proc_tlbi_local_all(void);
/*
 * Сбросить ASID TLB по идентификатору ASID на локальном процессоре
 */
void arch_proc_tlbi_local(proc_context_id_t id);
/*
 * Сбросить ASID TLB по идентификатору ASID и виртуальному адресу на локальном процессоре
 */
void arch_proc_tlbi_local_va(vir_bytes va, proc_context_id_t id);
/*
 * Сбросить весь ASID TLB на всех ядрах
 */
void arch_proc_tlbi_is_all(void);
/*
 * Сбросить ASID TLB по идентификатору ASID на на всех ядрах
 */
void arch_proc_tlbi_is(proc_context_id_t id);
/*
 * Сбросить ASID TLB по идентификатору ASID и виртуальному адресу на всех ядрах
 */
void arch_proc_tlbi_is_va(vir_bytes va, proc_context_id_t id);
/*
 * Записать контекст процесса
 */
void arch_proc_context_set(proc_context_id_t context);
/*
 * Записать контекст ядра
 */
void arch_proc_context_set_kernel(void);

#endif //REMINIX_PROC_HARDWARE_CONTEXT_H
