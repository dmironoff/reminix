//
// Created by dmironov on 02.04.2026.
//

#ifndef REMINIX_ARCH_SMP_H
#define REMINIX_ARCH_SMP_H

#include "bsp_cpu.h"
#include "cpufunc.h"

// На arm у нас пока идентификаторы ядер человеческие, но эта функция на всякий случай
// Так как в будущем мы конечно хотим реализовать кластеры ядер и их разнообразие
#define cpunr2cpuid(nr) nr
#define cpuid2cpunr(id) id

/*
 * Архитектурно зависимая часть запуска SMP
 * Типа хук из smp_init();
 */
void arch_smp_init();

/*
 * Подготовка к запуску ядра процессора
 * Передача управления на запущенном ядре нашему трамплину
 */
void arch_smp_boot_cpu(int cpunr, phys_bytes trampoline_entry);

/*
 * Физическое отключение одного из ядер
 * НЕ ЗАПУСКАТЬ НА ОТКЛЮЧАЕМОМ ЯДРЕ
 * на выключаемом ядре нужно закончить дела, вырубить прерывания, включить wfe
 */
void arch_smp_halt_cpu(int cpunr);

/*
 * Сюда мы должны вылететь из трамплина
 */
void arch_smp_cpu_second_init(void);

/*
 * Копирование трамплина в новую область памяти
 * Возвращает физический адрес для arch_smp_boot_cpu()
 */
phys_bytes arch_smp_copy_trampoline(mmap_t *mmap);

#define arch_stop_cpu() bsp_stop_cpu();

#endif //REMINIX_ARCH_SMP_H
