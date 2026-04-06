//
// Created by dmironov on 28.03.2026.
//

#include "kernel/kernel.h"
#include "cpufunc.h"
#include "arch_proc_context.h"
#include "spinlock.h"

#include <stdint.h>

extern int is_smp_mode; // Глобальная переменная, которая определяет наличие нескольких ядер процессора

static arch_context_allocator_t allocator = {0};

int is_asid_supported;

static SPINLOCK_IRQ(context);

/*
 * Инициализация механизма ASID
 */
int arch_proc_context_init(void) {
    is_asid_supported = ((read_mmfr0() & 0xFF) >= 3);
  if (!is_asid_supported) return;
      allocator.generation = 1;
      allocator.next_id = ARCH_PROC_CONTEXT_MIN_ID; // В нашей архитектуре системный сервер VM имеет иддентификатор 1 и он никогда не меняется
      spinlock_irq_init(context);
      arch_proc_context_set_kernel();
      if (is_smp_mode)
          arch_proc_tlbi_is_all();
      else
          arch_proc_tlbi_local_all();


      return 1;
}

/*
 * Назначить новый идентификатор контекста процессу
 * Защищена spinlock_irq
 */
void arch_proc_assign_context_id(struct proc *p) {
    if (!is_asid_supported) return;

    if (p->p_nr != VM_PROC_NR) {
        spinlock_irq_lock(context);
        if (allocator.next_id >= ARCH_PROC_CONTEXT_COUNT) {
            allocator.generation++;
            if (allocator.generation == 0) allocator.generation = 1; // Защита от переполнения переменной
            // Новый процесс всегда имеет значение generation 0, так мы вычисляем тех у кого ещё нет ID
            allocator.next_id = ARCH_PROC_CONTEXT_MIN_ID;

            spinlock_irq_unlock(context);
            if (is_smp_mode)
                arch_proc_tlbi_is_all();
            else
                arch_proc_tlbi_local_all();

            spinlock_irq_lock(context);
        }
        p->context_id.generation = allocator.generation;
        p->context_id = allocator.next_id++;

        spinlock_irq_unlock(context);

        if (is_smp_mode)
            arch_proc_tlbi_is(p->context_id);
        else
            arch_proc_tlbi_local(p->context_id);
    }
}

/*
 * Проверка валидности текущего идентификатора
 * Если нет, то она сама поставит новый идентификатор в процесс
 */
void arch_proc_check_context_id(struct proc *p) {
    if (!is_asid_supported) return;
    spinlock_irq_lock(context);
    // Если поколение устарело, либо процесс новый(поколение = 0) и при этом это идентификатор не резервирован
    if ((allocator.generation != p->context_id.generation) && (p->p_nr != VM_PROC_NR)) {
        spinlock_irq_unlock(context);
        arch_proc_assign_context_id(p);
        spinlock_irq_lock(context);
    }
    spinlock_irq_unlock(context);
}

/*
 * Универсальная функция для любых систем
 * сбросить весь asid
 */
void arch_proc_context_flush() {
    if (!is_asid_supported) return;
    if (is_smp_mode)
        arch_proc_tlbi_is_all();
    else
        arch_proc_tlbi_local_all();
}

/*
 * Универсальная функция для любых систем
 * сбросить asid для идентификатора
 */
void arch_proc_context_id_flush(proc_context_id_t id) {
    if (!is_asid_supported) return;
    if (is_smp_mode)
        arch_proc_tlbi_is(id);
    else
        arch_proc_tlbi_local(id);
}

/*
 * Сбросить весь ASID TLB на локальном процессоре
 */
void arch_proc_tlbi_local_all(void) {
    if (!is_asid_supported) return;
    tlbi_all_local();
}

/*
 * Сбросить ASID TLB по идентификатору ASID на локальном процессоре
 */
void arch_proc_tlbi_local(proc_context_id_t id) {
    if (!is_asid_supported) return;
    tlbi_asid_local(id.id);
}

/*
 * Сбросить ASID TLB по идентификатору ASID и виртуальному адресу на локальном процессоре
 */
void arch_proc_tlbi_local_va(vir_bytes va, proc_context_id_t id) {
    if (!is_asid_supported) return;
    tlbi_mva_asid_local((uint32_t) va , id.id);
}

/*
 * Сбросить весь ASID TLB на всех ядрах
 */
void arch_proc_tlbi_is_all(void) {
    if (!is_asid_supported) return;
    tlbi_all_is();
}

/*
 * Сбросить ASID TLB по идентификатору ASID на на всех ядрах
 */
void arch_proc_tlbi_is(proc_context_id_t id) {
    if (!is_asid_supported) return;
    tlbi_asid_is(id.id);
}

/*
 * Сбросить ASID TLB по идентификатору ASID и виртуальному адресу на всех ядрах
 */
void arch_proc_tlbi_is_va(vir_bytes va, proc_context_id_t id) {
    if (!is_asid_supported) return;
    tlbi_mva_asid_is((uint32_t) va, id.id);
}

/*
 * Записать контекст процесса
 */
void arch_proc_context_set(proc_context_id_t context) {
    if (!is_asid_supported) return;
    write_asid(context.id);
}

/*
 * Записать контекст ядра
 */
void arch_proc_context_set_kernel(void) {
    if (!is_asid_supported) return;
    write_asid(ARCH_PROC_CONTEXT_KERNEL_ID);
}