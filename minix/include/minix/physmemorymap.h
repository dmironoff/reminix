//
// Created by dmironov on 19.03.2026.
//

/*
 * Здесь мы описываем новую структуру данных для нашей абстрактной карты памяти
 * Именно на основе этой карты vm будет принимать решение о выделении конкретных адресов памяти
 */

#ifndef REMINIX_PHYSMEMORYMAP_H
#define REMINIX_PHYSMEMORYMAP_H

#define MMAP_MAX_REFSCOUNT          10

typedef u32_t mmap_cache_hint_t;
#define MMAP_CACHE_NORMAL       0
#define MMAP_CACHE_NO           1
#define MMAP_CACHE_WRITECOMB    2
#define MMAP_CACHE_WRITETHROUGH 3
#define MMAP_CACHE_DMA          4

typedef uint32_t mmap_region_flags;

#define MMAP_REGION_DEVICE_INDIVIDUAL      1

// Абстрактные типы памяти
typedef enum {
    MMAP_UNDEF      = 0,
    MMAP_FREE       = 1,
    MMAP_KERNEL     = 2,
    MMAP_RESERVED   = 3,
    MMAP_DEVICE     = 4,
    MMAP_DMA        = 5,
    MMAP_KRNL_APT   = 6,
    MMAP_KRNL_MMAP  = 7,
    MMAP_BOOT_MOD   = 8,
    MMAP_FDT        = 9,   // Память FDT - используется только на arm и risc-v
    MMAP_PAGETABLES = 10,  // Память выделенная для хранения всех таблиц страниц системы
    MMAP_BKI        = 11,
    MMAP_SMP_TRAMPOLINE = 12,
    MMAP_ALLOCATED  = 13,
} mmap_type_t;

/* Регион памяти, технически может быть любого размера
 * Когда из него выделяется память, то он просто дробиться и рядом создаётся ещё одна запись о регионе
 */
typedef struct {
    phys_bytes          start;
    phys_bytes          size;
    mmap_type_t         type;
    mmap_cache_hint_t   cache_hint;
    mmap_region_flags   flags;
    uint32_t            refcount;
    endpoint_t          refs[MMAP_MAX_REFSCOUNT]; // Положим сюда список процессов, которые используют этот регион памяти
    void                *prev_region;
    void                *next_region;
} mmap_region_t;

/*
 * Полная карта физической памяти с которой будет работать VM
 */
typedef struct {
    uint32_t            version;
    uint32_t            need_defragmentation;  // Пометочка для системы, что бы она в свободное время занялась дефрагментацией
    phys_bytes          l1_page_size;
    phys_bytes          total_mem;          // В это число не будет входить адресное пространство устройств
    phys_bytes          free_mem;
    uint32_t            regions_allocated;
    uint32_t            regions_count;
    mmap_region_t       *first_region;
    mmap_region_t       *last_region;
    mmap_region_t       *regions;           // Ссылка на линейный массив регионов,
                                            // Мы постараемся его держать в порядке и во время idle приводить в линейное состояние
} mmap_t;

#endif //REMINIX_PHYSMEMORYMAP_H
