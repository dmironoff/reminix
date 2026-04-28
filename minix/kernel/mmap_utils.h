//
// Created by dmironov on 20.03.2026.
//

#ifndef REMINIX_MMAP_UTILS_H
#define REMINIX_MMAP_UTILS_H

#define MMAP_ERROR_NOT_AVALIBLE_REGIONS     -1
#define MMAP_ERROR_INCORRECT_ADDR           -2
#define MMAP_ERROR_INDIVIDUAL_ONLY          -3
#define MMAP_ERROR_REGION_BUSY              -4
#define MMAP_ALIGNMENT_ERROR                -5


#include <minix/physmemorymap.h>

/*
 * Один из двух основных механизма ядра - абстрактная карта памяти
 * Сюда мы вносим информацию обо всём использовании физической памяти
 * Так же VM будет следить за количеством использующих эти регионы процессов
 */


/*
 * Поиск региона памяти по адресу памяти
 * Возвращает указатель на регион памяти в карте через переменную region
 */
int mmap_find_region_by_addr(mmap_t *mmap, phys_bytes addr, mmap_region_t *region);
/*
 * Инициализация чистой карты памяти
 * Предполагает, что указатель на массив свободных записей уже установлен
 *
 */
int mmap_init(mmap_t *mmap, phys_bytes page_size, phys_bytes size);
/*
 * Выделение памяти для устройств
 * Если попадает на свободную память, то порежет регион и уменьшие данные о свободной памяти
 * На свободную память эта функция попадает только при инициализации системы, когда мы имеем один большой "прото-регион"
 * Если на память устройств, то просто порежет регион - это для выделения отдельных регионов драйверам для счёта refs
 */
int mmap_alloc_device(mmap_t *mmap, phys_bytes start, phys_bytes size, mmap_region_t *region);
/*
 * Выделение памяти для dma
 * Если попадает на свободную память, то порежет регион и уменьшие данные о свободной памяти
 * На свободную память эта функция попадает только при инициализации системы, когда мы имеем один большой "прото-регион"
 * Если на память dma, то просто порежет регион - это для выделения отдельных регионов драйверам для счёта refs
 */
int mmap_alloc_dma(mmap_t *mmap, phys_bytes start, phys_bytes size, mmap_region_t *region);
/*
 * Выделение памяти из "прото-региона" для зарезервированных железкой адресов
 * Функция вызывается один раз при инициализации системы
 */
int mmap_alloc_reserved(mmap_t *mmap, phys_bytes start, phys_bytes size, mmap_region_t *region);
/*
 * Копирование карты памяти по новому адресу
 * Используется при инициализации системы
 */
int mmap_copy_to_new_location(mmap_t *mmap, mmap_t *new_mmap);
/*
 * Выделение нового региона памяти
 * Возвращает указатель на структуру описывающую новый регион через последнюю переменную
 */
int mmap_alloc_region(mmap_t *mmap, phys_bytes start, phys_bytes size, mmap_region_t *region);
/*
 * Выделяет самый низкий из свободных регионов памяти размеров size
 * Возвращает указатель на новый регион через переменнную
 * Адрес старта региона будет в возвращаемой указателем структуре
 */
int mmap_alloc_lowest_region(mmap_t *mmap, phys_bytes size, mmap_region_t *region);
/*
 * Выделяет самый высокий из свободных регионов памяти размеров size
 * Возвращает указатель на новый регион через переменнную
 * Адрес старта региона будет в возвращаемой указателем структуре
 */
int mmap_alloc_highest_region(mmap_t *mmap, phys_bytes size, mmap_region_t *region);
/*
 * Освободить память
 * Ядро может освободить любую память кроме DEVICE, DMA и RESERVED
 * Свободные регионы памяти автоматически объеденияются
 * start и size могут попадать на середину занятых регионов, но всегда должны быть выровнены по l2_page_size
 * Если область залезает на MMAP_FREE, то это никак не влияет на работу функции
 */
int mmap_free_memory(mmap_t *mmap, phys_bytes start, phys_bytes size);
/*
 * Функция для итерации регионов по типу
 * Никаких проверок выравнивания, просто: 0 - ненайдено, 1 - найден регион
 */
int mmap_find_next_by_type(mmap_t *mmap, mmap_type_t type, phys_bytes offset, mmap_region_t *region);
/*
 * Выравнивание адреса или размера по странице l2
 */
phys_bytes mmap_align(mmap_t *mmap, phys_bytes value);

#endif //REMINIX_MMAP_UTILS_H
