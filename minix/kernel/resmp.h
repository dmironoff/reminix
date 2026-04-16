//
// Created by dmironov on 02.04.2026.
//

#ifndef REMINIX_RESMP_H
#define REMINIX_RESMP_H

#include "kernel/kernel.h"
#include "arch_smp.h"
#include "spinlock.h"
#include "kernel/ipi.h"

EXT_SPINLOCK(big_kernel_lock);

// Глобальный флаг и переменная с количеством процессоров, которые мы кучи раз будем проверять
// Объявлены в main.c
extern int is_smp_mode; // 0 - Uniprocessor mode, 1 - SMP
extern int cpu_count; // По умолчанию у нас всего один cpu
extern int bsp_cpu_nr; // Номер процессора на котором мы запустились

#define cpu_is_bsp(nr) (bsp_cpu_nr == nr)

#define CPU_IS_BSP	1
#define CPU_IS_READY	2

struct cpu {
    u32_t flags;
};

EXTERN struct cpu cpus[CONFIG_MAX_CPUS];

#define cpu_set_flag(cpu, flag)	do { cpus[cpu].flags |= (flag); } while(0)
#define cpu_clear_flag(cpu, flag) do { cpus[cpu].flags &= ~(flag); } while(0)
#define cpu_test_flag(cpu, flag) (cpus[cpu].flags & (flag))
#define cpu_is_ready(cpu) cpu_test_flag(cpu, CPU_IS_READY)

// Приколюшка для быстрого определения текущего ядра
#define cpunr cpuid2cpunr(arch_get_current_cpuid())


int smp_ipi_irq_handler(struct irq_hook *hook);

void smp_tss_init_all(void);

void smp_ap_boot(int cpu_nr);

void smp_init(void);

void smp_ap_init(void);

/*
 * мы гасим все дополнительные ядра
 * кроме себя.
 */
void smp_shutdown_aps(void);

/*
 * Вызовы для других частей системы, что бы выполнять нужные действия с нашими всеми ядрами
 * Весь прикол, что они проверяют многоядерность системы и мы их можем не опасаясь вызывать из любого места ядра
 */

/*
 * Сбросить кеш контекста процесса у всех ядер
 */
void proc_context_shoot_all(struct proc *p);

/*
 * Отправить вызов дополнительной функции на ядро
 */
void ipi_send_call(int cpu_nr, int func, void *data);

/*
 * Отправить вызов дополнительной функции всем
 */
void ipi_send_call_all(int func, void *data);

/*
 * Отправить всем ядрам команду на переход в остановленное состояние для паники
 */
void ipi_send_stop(void);

/*
 * Пересчитать очередь на cpu
 */
void ipi_send_reschedule(int cpu_nr);

/*
 * Пересчитать очередь всем
 */
void ipi_send_reschedule_all(void);
/*
 * Остановить выполнение процесса
 */
void ipi_send_stop_proc(struct proc *p);

/*
 * Остановить процесс и полностью сохранить контекс
 */
void ipi_send_save_ctx(struct proc *p);

/*
 * Остановить процесс пока VM не разберётся с его таблицей страниц
 * Используется когда требуется переразметить пространство процесса
 */
void ipi_send_vm_inhibit(struct proc *p);

#endif //REMINIX_RESMP_H
