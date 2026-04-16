//
// Created by dmironov on 20.03.2026.
//

/*
 * Наша небольшая библиотека по работе с абстрактными таблицами страниц виртуальной памяти
 * Возможно, так же станет и куском нового VM
 *
 * И ДА! ЭТО МНОГО ВЕТВЛЕНИЯ И ОПЕРАЦИЙ, НО ЭТО ЕДИНСТВЕННОЕ ЛЕКАРСТВО ДЛЯ СВОБОДНОЙ ПОДДЕРЖКИ МНОГИХ АРХИТЕКТУР
 *
 * Это как общий для всей системы язык описания виртуальной памяти, а mmap это общий язык для описания физической памяти
 *
 * НЕ ЗАБУДЬ СТАВИТЬ СПИНЛОКИ ПОСЛЕ ЗАПУСКА SMP
 *
 */


#ifndef REMINIX_APT_UTILS_H
#define REMINIX_APT_UTILS_H

#include <minix/abstract_pagetables.h>

#define APT_ERROR_NO_MORE_UNDEF_RECORDS       -1
#define APT_NOT_FOUND                         -2
#define APT_DEFRAGMENTATION_ERROR             -3
#define APT_NOT_FREE                          -4
#define APT_ALIGNMENT_ERROR                   -5

/*
 * Поиск таблицы по её владельцу
 */
int apt_find_table_by_endpoint(vm_abstract_pagetables_t *apt, endpoint_t endpoint, vm_abstract_pt_t *table);

/*
 * Поиск таблицы по её физическому адресу
 */
int apt_find_table_by_phys_addr(vm_abstract_pagetables_t *apt, phys_bytes addr, vm_abstract_pt_t *table);

/*
 * Удаление таблицы страниц, например в связи с преркращением процесса,
 * И возврат их записей в пулл свободных
 */
int apt_unmap_table(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table);

/*
 * Поиск l1 секции в таблице страниц по виртуальному адресу
 */
int apt_find_l1_entry_by_virt_addr(vm_abstract_pt_t *table, vir_bytes addr, vm_abstract_pt_l1_entry_t *l1_entry);

/*
 * Поиск l1 секции в таблице страниц по физическому адресу
 */
int apt_find_l1_entry_by_phys_addr(vm_abstract_pt_t *table, phys_bytes addr, vm_abstract_pt_l1_entry_t *l1_entry);

/*
 * Поиск l2 страницы в l1 секции по виртуальному адресу
 */
int apt_find_l2_entry_by_virt_addr(vm_abstract_pt_l1_entry_t *l1_entry, vir_bytes addr, vm_abstract_pt_l2_entry_t *l2_entry);

/*
 * Поиск l2 страницы в l1 секции по физическому адресу
 */
int apt_find_l2_entry_by_phys_addr(vm_abstract_pt_l1_entry_t *l1_entry, phys_bytes addr, vm_abstract_pt_l2_entry_t *l2_entry);

/*
 * Размапить полностью пустую новую таблицу памяти
 */
int apt_make_clean_table(vm_abstract_pagetables_t *apt, endpoint_t owner, vm_abstract_pt_t *new_table);

/*
 * Общая функция для проверки свободного места в виртуальной памяти
 * При помощи этой функции мы будем защищать другие функции разметки
 */
int apt_check_free_memory_addr (vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes start, vir_bytes size);

/*
 * Размапить адреса физической памяти по виртуальному адресу
 * Функция проверяет размер и выделяет виртуальную память по запрошенному адресу
 * Адрес и размер должны быть выровнеными по границе l2
 */
int apt_map_phys_to_vir(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, phys_bytes addr, vir_bytes size,
                        vir_bytes start, vm_apt_flags_t flags, mmap_cache_hint_t cache_hint);

/*
 * Размапить регион карты памяти по виртуальному адресу
 * Функция проверяет размер и выделяет виртуальную память по запрошенному адресу
 * Адрес и размер должны быть выровнеными по границе l2
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
int apt_map_on_demand(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes start, vir_bytes size,
                      vm_apt_flags_t flags, mmap_cache_hint_t cache_hint);

/*
 * Освободить виртуальную память
 * Функция на автомате производит конкатенацию свободного пространства в целиковые регионы
 * Как всегда, адреса и размеры должны быть выровнены по apt->l2_page_size
 * Но, кстати,этой функции похрену используется это пространство или нет, просто на выходе получится
 * Одно большое свободное пространство, если по краям у него тоже свободное пространство, оно сливается воедино
 * start и size могут попадать в середину записи l1 тогда мы разделим эти записи на l2pt
 */
int apt_unmap_vir_addr(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes start, vir_bytes size);

/*
 * Удалить даные о физических адресах из таблицы страниц
 * Функция предполагает, что регион размечен непрерывно
 * Является обёрткой для apt_unmap_vir_addr, просто вычисляет стартовый виртуальный адрес от физического адреса старта
 * см. описание apt_unmap_vir_addr для понимания поведения
 */
int apt_unmap_phys_addr(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, phys_bytes start, phys_bytes size);

/*
 * Удалить даные о регионе из таблицы страниц
 * Обёртка для apt_unmap_phys_addr(apt_unmap_vir_addr)
 * см. описание apt_unmap_phys_addr и apt_unmap_vir_addr для понимания механизма работы
 */
int apt_unmap_region(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, mmap_region_t *region);

/*
 * Размапить полностью пустую новую таблицу памяти с определённым количеством страниц l1
 */
int apt_make_clean_table_with_size(vm_abstract_pagetables_t *apt, endpoint_t owner, int l1_pages, vm_abstract_pt_t *new_table);

#endif //REMINIX_APT_UTILS_H
