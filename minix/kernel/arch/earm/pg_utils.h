//
// Created by dmironov on 13.04.2026.
//


/*
 * Все функции в этом файле используются только для инициализации системы
 * Дальше действует связка - mmap -> apt -> pagetables.c
 * А сейчас нам нужен набор утилит для работы с чистой таблицей страниц архитектуры
 * Что бы разместить в памяти все наши структуры данных при инициализации
 */



#ifndef REMINIX_PG_UTILS_H
#define REMINIX_PG_UTILS_H

#include <minix/cpufeature.h>

#include <minix/type.h>
#include <assert.h>
#include "kernel/kernel.h"
#include "arch_proto.h"
#include <machine/cpu.h>
#include <arm/armreg.h>

#include <string.h>
#include <minix/type.h>

#include "bsp_serial.h"
#include <minix/physmemorymap.h>
#include "kernel/mmap_utils.h"

#include "pagetables.h"
#include "arch_configs.h"


/*
 * Возвращает физический адрес инициализационной таблицы страниц
 */
phys_bytes pg_get_phys_addr(void);
/*
 * Разметить регион с памятью устройств 1 к 1
 */
void pg_map_device_region(mmap_region_t *region);
/*
 * Разметить обычный регион 1 к 1
 * При инициализации мы размечаем всю память как исполняему в привелигированном режиме
 */
void pg_map_mem_region(mmap_region_t *region);
/*
 * Разметить виртуальный регион с исполняемым кодом ядра
 * Нам это нужно исключительно что бы когда мы будем пихать в свободную виртуальную память
 * наши структуры данных мы уже имели там размапленное ядро и не перезаписали его случайно
 */
void pg_map_kern_region (mmap_region_t *region);
/*
 * Инициализация страниц виртуальной памяти с разметкой 1 к 1
 */
void pg_init(mmap_t *mmap);
/*
 * Размапливание региона по определённому виртуальному адресу
 * Не проверяет на свободное место, просто вхуючивает.
 */
void pg_map_region_to_vir (mmap_region_t *region, vir_bytes base);

/*
 * Разметка виртуального адреса для доступа к новым структурам ядра
 * Находим первый свободный диапозон с конца виртуальной памяти размером с размер региона
 * и мапим туда регион
 * потом возвращаем новый виртуальный адрес старта
 * Задача: размапиться как можно более компактно, то есть влезть в страницы 4кб в самый притык
 * Поэтому мы не будем выравнивать наши виртуальные адреса по 1мб, а выровняемся по 4 кб
 * */
vir_bytes pg_map_high(mmap_region_t *region);

#endif //REMINIX_PG_UTILS_H
