//
// Created by dmironov on 19.03.2026.
//

/*
 * Здесь мы описываем новую структуру данных для нашей абстрактной карты памяти
 * Именно на основе этой карты vm будет принимать решение о выделении конкретных адресов памяти
 */

#ifndef REMINIX_PHYSMEMORYMAP_H
#define REMINIX_PHYSMEMORYMAP_H

#define MMAP_MAX_REFSCOUNT          30  // Количество процессов которые могут использовать этот регион памяти

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
    MMAP_UNDEF      = 0,  // Запись свободна
    MMAP_FREE       = 1,  // Свободная память
    MMAP_KERNEL     = 2,  // Ядро
    MMAP_RESERVED   = 3,  // Зарезервированная память
    MMAP_DEVICE     = 4,  // Устройства
    MMAP_DMA        = 5,  // DMA
    MMAP_KRNL_APT   = 6,  // Общие для VM и ядра абстрактные таблицы страниц
    MMAP_KRNL_MMAP  = 7,  // Общий для VM и ядра регион с вот этой самой картой памяти
    MMAP_BOOT_MOD   = 8,   // Загрузочный модуль - например ещё не запущенный сервер
    MMAP_FDT        = 9,   // Память FDT - используется только на arm и risc-v
    MMAP_PAGETABLES = 10,  // Память выделенная для хранения всех таблиц страниц системы
    MMAP_BKI        = 11,  // Память выделенная под загрузочную информацию передаваемую от pre_init в kmain
    MMAP_SMP_TRAMPOLINE = 12, // Трамплин для запуска дополнительных ядер процессора
    MMAP_ALLOCATED  = 13,  // Память выделенная для пользовательского приложения
} mmap_type_t;

/* Регион памяти, может быть любого размера
 * Когда из него выделяется память, то он просто дробиться и рядом создаётся ещё одна запись о регионе
 */
typedef struct {
    phys_bytes          start;          // Стартовый адрес
    phys_bytes          size;           // размер
    mmap_type_t         type;          // Тип
    mmap_cache_hint_t   cache_hint;   // Рекомендации к кешированию
    mmap_region_flags   flags;         // Флаги
    uint32_t            refcount;       // Количество активных пользователей этой памяти
    endpoint_t          refs[MMAP_MAX_REFSCOUNT]; // Положим сюда список процессов, которые используют этот регион памяти
    void                *prev;  // Ссылка на следующий размеченный регион
    void                *next;  // ссылка на предыдущий размеченный регион
} mmap_region_t;

/*
 * Полная карта физической памяти с которой будет работать VM
 */
typedef struct {
    kmutex_t             lock;
    uint32_t            version;                // Версия таблицы для синхронизации
    phys_bytes          l2_page_size;    // Выравнивание памяти по размеру l2 страницы MMU процессора
    phys_bytes          total_mem;          // В это число не будет входить адресное пространство устройств
    phys_bytes          free_mem;           // Остаток свободной физической памяти
    uint32_t            regions_allocated;  // Количество записей для которых выделенна память
    uint32_t            regions_count;   // Количество размеченных записей о регионах памяти
    mmap_region_t       *first_region;  // Ссылка на первый размеченный регион
    mmap_region_t       *last_region;   // Сссылка на последний регион
    mmap_region_t       *regions;           // Ссылка на линейный массив регионов
} mmap_t;

#endif //REMINIX_PHYSMEMORYMAP_H
