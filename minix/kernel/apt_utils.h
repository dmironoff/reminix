//
// Created by dmironov on 20.03.2026.
//


#ifndef REMINIX_APT_UTILS_H
#define REMINIX_APT_UTILS_H

#include <minix/abstract_pagetables.h>

#define APT_ERROR_NO_MORE_UNDEF_RECORDS       -1
#define APT_NOT_FOUND                         -2
#define APT_DEFRAGMENTATION_ERROR             -3
#define APT_NOT_FREE                          -4
#define APT_ALIGNMENT_ERROR                   -5

/*
 * Наша небольшая библиотека по работе с абстрактными таблицами страниц виртуальной памяти
 * Так же станет и куском нового VM
 *
 * И ДА! ЭТО МНОГО ВЕТВЛЕНИЯ И ОПЕРАЦИЙ, НО ЭТО ЕДИНСТВЕННОЕ ЛЕКАРСТВО ДЛЯ СВОБОДНОЙ ПОДДЕРЖКИ МНОГИХ АРХИТЕКТУР
 *
 * Это как общий для всей системы язык описания виртуальной памяти, а mmap это общий язык для описания физической памяти
 *
 * НЕ ЗАБУДЬ СТАВИТЬ СПИНЛОКИ ПОСЛЕ ЗАПУСКА SMP
 *
 */


/*
 * Удаление таблицы страниц, например в связи с преркращением процесса,
 * И возврат их записей в пулл свободных
 */
int apt_unmap_table(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table);

/*
 * Поиск записи о странице в таблице страниц по виртуальному адресу
 */
int apt_find_entry_by_virt_addr(vm_abstract_pt_t *table, vir_bytes addr, vm_abstract_pt_entry_t *entry);

/*
 * Поиск страницы в таблице страниц по физическому адресу
 */
int apt_find_entry_by_phys_addr(vm_abstract_pt_t *table, phys_bytes addr, vm_abstract_pt_entry_t *entry);

/*
 * Поиск страницы в таблице страниц по физическому диапазону
 * Возвращает первую попавшуюся запись подходящую под диапазон
 */
int apt_find_entry_by_phys_range(vm_abstract_pt_t *table, phys_bytes start, phys_bytes end, vm_abstract_pt_entry_t *entry);


/*
 * Размапить полностью пустую новую таблицу памяти
 */
int apt_make_clean_table(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *new_table);

/*
 * Объединение соседних записей о страницах.
 * Параметры берутся из первой записи
 * ВЕРСИОНИРОВАНИЯ НЕТ
 */
static inline int concat (vm_abstract_pt_t *table,
                          vm_abstract_pt_entry_t *first, vm_abstract_pt_entry_t *second);

/*
 * Откусить или выгрызть новую секцию из большой записи о страницах
 * Все параметры берутся из родительской записи
 */
static inline int biteoff (vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes start, vir_bytes size,
                           vm_abstract_pt_entry_t *from, vm_abstract_pt_entry_t *new);


/*
 * Общая функция для проверки свободного места в виртуальной памяти
 * При помощи этой функции мы будем защищать другие функции разметки
 */
int apt_check_free_memory_addr (vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes start, vir_bytes size);

/*
 * Размапить адреса физической памяти по виртуальному адресу
 * Функция проверяет размер и выделяет виртуальную память по запрошенному адресу
 * Адрес и размер должны быть выровнеными по границе page size
 */
int apt_map_phys_to_vir(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, phys_bytes addr, vir_bytes size,
                        vir_bytes start, vm_apt_flags_t flags, mmap_cache_hint_t cache_hint);

/*
 * Размапить регион карты памяти по виртуальному адресу
 * Функция проверяет размер и выделяет виртуальную память по запрошенному адресу
 * Адрес и размер должны быть выровнеными по границе страницы
 */
int apt_map_region_to_addr(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, mmap_region_t *region,
                           vir_bytes start, vm_apt_flags_t flags);

/*
 * Размапить адреса физической памяти от мксимального свободного адреса сконца
 * Функция проверяет размер и выделяет виртуальную память по максимальному адресу сконца с учётом размера региона
 * start - результат, младший адрес непрерывного пространства размером size, куда мы размапили addr
 */
int apt_map_phys_to_vir_max_free_end(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, phys_bytes addr, phys_bytes size,
                                     vir_bytes *start, vm_apt_flags_t flags, mmap_cache_hint_t cache_hint);


/*
 * Размапить адреса физической памяти от минимально свободного адреса сначала
 * Функция проверяет размер и выделяет виртуальную память по минимальному адресу сначала с учётом размера региона
 * start - результат, младший адрес непрерывного пространства размером size, куда мы размапили addr
 */
int apt_map_phys_to_vir_min_free_start(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, phys_bytes addr, phys_bytes size,
                                       vir_bytes *start, vm_apt_flags_t flags, mmap_cache_hint_t cache_hint);

/*
 * Размапить регион из карты памяти от максимально свободного адреса сконца
 * Функция проверяет размер и выделяет виртуальную память по максимальному адресу с конца с учётом размера региона
 */
int apt_map_region_to_max_free_end(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, mmap_region_t *region,
                                   vir_bytes *start, vm_apt_flags_t flags);

/*
 * Размапить регион из карты памяти от минимально свободного адреса снизу
 * Функция проверяет размер и выделяет виртуальную память по минимальному адресу начала с учётом размера региона
 */
int apt_map_region_to_min_free_start(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, mmap_region_t *region,
                                     vir_bytes *start, vm_apt_flags_t flags);

/*
 * Размапить память для страниц выделяемых по требованию
 * Страницы по требованию у нас имеют ФЛАГ VM_APF_VIRTUAL_ONLY и физический адрес 0
 */
int apt_map_on_demand(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes start, vir_bytes size);

/*
 * Освободить виртуальную память
 * Функция на автомате производит конкатенацию свободного пространства в целиковые регионы
 * Как всегда, адреса и размеры должны быть выровнены по apt->page_size
 * Но, кстати,этой функции похрену используется это пространство или нет, просто на выходе получится
 * Одно большое свободное пространство, если по краям у него тоже свободное пространство, оно сливается воедино
 * start и size могут попадать в середину записи
 */
int apt_unmap_vir_addr(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes start, vir_bytes size);

/*
 * Освобождение всех страниц использующих физические адреса
 * РАЗМАПЛИВАЕТ ВСЕ ИСПОЛЬЗОВАНИЯ ЭТИХ АДРЕСОВ НЕЗАВИСИМО ОТ ИХ КОЛИЧЕСТВА
 */
int apt_unmap_phys_addr(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, phys_bytes start, phys_bytes size);

/*
 * Удалить даные о регионе из таблицы страниц
 * Обёртка для apt_unmap_phys_addr(apt_unmap_vir_addr)
 * см. описание apt_unmap_phys_addr и apt_unmap_vir_addr для понимания механизма работы
 */
int apt_unmap_region(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, mmap_region_t *region);

/*
 * Изменить флаги для виртуальных адресов
 */
int apt_vir_set_new_flags(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes start, vir_bytes size, vm_apt_flags_t flags, mmap_cache_hint_t cache);

#endif //REMINIX_APT_UTILS_H
