//
// Created by dmironov on 19.03.2026.
//

#include "string.h"
#include <sys/types.h>
#include <minix/endpoint.h>
#include "apt_utils.h"
#include <minix/physmemorymap.h>

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
 * Жесткое добавление новой записи в таблицу c начала
 * Используется при копировании
 */
static int apt_hard_add_entry_first(vm_abstract_pt_t *table, vm_abstract_pt_entry_t *entry) {
    if (table->first == 0) {
        table->first = entry;
        table->last = entry;
    } else {
        table->first->prev = entry;
        entry->next = table->first;
        table->first = entry;
    }
    return OK;
}

/*
 * Жесткое добавление новой записи в таблицу c конца
 * Используется при копировании
 */
static int apt_hard_add_entry_last(vm_abstract_pt_t *table, vm_abstract_pt_entry_t *entry) {
    if (table->first == 0) {
        table->first = entry;
        table->last = entry;
    } else {
        table->last->next = entry;
        entry->prev = table->last;
        table->last = entry;
    }
    return OK;
}

/*
 * Поиск первой незанятой записи о таблице
 */
static int apt_find_undef_table(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table) {
    if (apt->pagetables_allocated <= apt->pagetables_used) {
        return APT_ERROR_NO_MORE_UNDEF_RECORDS;
    }
    for (int i = 0; i < apt->pagetables_allocated; i++) {
        if (apt->tables[i].status == VM_RECORD_UNDEF) {
            apt->tables[i].status = VM_RECORD_INUSE;
            table = &apt->tables[i];
            return OK;
        }
    }
    return APT_DEFRAGMENTATION_ERROR;
}

/*
 *   поиск первой незанятой записи о страницах
 */
static int apt_find_undef_entry(vm_abstract_pagetables_t *apt, vm_abstract_pt_entry_t *entry) {
    if (apt->entries_allocated <= apt->entries_used) {
        return APT_ERROR_NO_MORE_UNDEF_RECORDS;
    }
    for (int i = 0; i < apt->entries_allocated; i++) {
        if (apt->entries[i].status == VM_RECORD_UNDEF) {
            apt->entries[i].status = VM_RECORD_INUSE;
            entry = &apt->entries[i];
            return OK;
        }
    }
    return APT_DEFRAGMENTATION_ERROR;
}


/*
 * Физическое исключение записи о страницах из таблицы страниц
 * Низкоуровневая функция, используй функции более высокого уровня для переразметки памяти
 */
static int apt_unmap_entry(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vm_abstract_pt_entry_t *entry) {
    if (entry->next != 0) {
        ((vm_abstract_pt_entry_t *) entry->next)->prev = entry->prev;
    } else {
        table->last = entry->prev;
    }
    if (entry->prev != 0) {
        ((vm_abstract_pt_entry_t *) entry->prev)->next = entry->next;
    } else {
        table->first = entry->next;
    }

    memset(entry, 0, sizeof(vm_abstract_pt_entry_t));

    return OK;
}

/*
 * Удаление таблицы страниц, например в связи с преркращением процесса,
 * И возврат их записей в пулл свободных
 */
int apt_unmap_table(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table) {
    vm_abstract_pt_entry_t *iter;

    for (iter = table->first; iter != 0; iter = table->first) {
        apt_unmap_entry(apt, table, iter);
    }

    if (table->next != 0) {
        ((vm_abstract_pt_t *) table->next)->prev = table->prev;
    } else {
        apt->last = table->prev;
    }

    if (table->prev != 0) {
        ((vm_abstract_pt_t *) table->prev)->next = table->next;
    } else {
        table->first = table->next;
    }

    memset(table, 0, sizeof(vm_abstract_pt_t));

    return OK;
}

/*
 * Поиск записи о странице в таблице страниц по виртуальному адресу
 */
int apt_find_entry_by_virt_addr(vm_abstract_pt_t *table, vir_bytes addr, vm_abstract_pt_entry_t *entry) {
    vm_abstract_pt_entry_t *iter;

    for (iter = table->first; iter != 0; iter = (vm_abstract_pt_entry_t *) iter->next) {
        if (iter->status != VM_RECORD_UNDEF && iter->vaddr <= addr && addr <= iter->vaddr + iter->size) {
            entry = iter;
            return OK;
        }
    }
    return APT_NOT_FOUND;
}

/*
 * Поиск страницы в таблице страниц по физическому адресу
 */
int apt_find_entry_by_phys_addr(vm_abstract_pt_t *table, phys_bytes addr, vm_abstract_pt_entry_t *entry) {
    vm_abstract_pt_entry_t *iter;

    for (iter = table->first; iter != 0; iter = (vm_abstract_pt_entry_t *) iter->next) {
        if (iter->status != VM_RECORD_UNDEF && iter->paddr <= addr && addr <= iter->paddr + iter->size) {
            entry = iter;
            return OK;
        }
    }
    return APT_NOT_FOUND;
}

/*
 * Поиск страницы в таблице страниц по физическому диапазону
 * Возвращает первую попавшуюся запись подходящую под диапазон
 */
int apt_find_entry_by_phys_range(vm_abstract_pt_t *table, phys_bytes start, phys_bytes end, vm_abstract_pt_entry_t *entry) {
    vm_abstract_pt_entry_t *iter;

    for (iter = table->first; iter != 0; iter = (vm_abstract_pt_entry_t *) iter->next) {
        if (iter->status != VM_RECORD_UNDEF &&
                (iter->paddr <= start && (iter->paddr + iter->size <= end || iter->paddr + iter->size >= end))) {
            entry = iter;
            return OK;
        }
    }
    return APT_NOT_FOUND;
}


/*
 * Размапить полностью пустую новую таблицу памяти
 */
int apt_make_clean_table(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *new_table) {
    int res;
    vm_abstract_pt_entry_t *entry;

    res = apt_find_undef_table(apt, new_table);
    if (res != OK) {
        return res;
    }

    res = apt_find_undef_entry(apt, entry);
    if (res != OK) {
        return res;
    }

    new_table->first = entry;
    new_table->last = entry;
    entry->status = VM_RECORD_FREE;
    entry->paddr = 0;
    entry->vaddr = 0;
    entry->size = apt->page_size * apt->pages_max_count;
    entry->flags = 0;
    entry->cache = 0;
    table->version = 1;

    return OK;
}


/*
 * Объединение соседних записей о страницах.
 * Параметры берутся из первой записи
 * ВЕРСИОНИРОВАНИЯ НЕТ
 */
static inline int concat (vm_abstract_pt_t *table,
                             vm_abstract_pt_entry_t *first, vm_abstract_pt_entry_t *second) {
    if (first->next != (void *)second) {
        return EINVAL;
    }
    if (second->next != 0) {
        first->next = second->next;
        ((vm_abstract_pt_entry_t *)second->next)->prev = first;
    } else {
        first->next = 0;
        table->last = first;
    }

    first->size += second->size;

    memset(second, 0, sizeof(vm_abstract_pt_entry_t));

    return OK;
}


/*
 * Откусить или выгрызть новую секцию из большой записи о страницах
 * Все параметры берутся из родительской записи
 */
static inline int biteoff (vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes start, vir_bytes size,
                              vm_abstract_pt_entry_t *from, vm_abstract_pt_entry_t *new) {
    vm_abstract_pt_entry_t *next;
    int res;

    if (start % apt->page_size || size % apt->page_size) {
        return APT_ALIGNMENT_ERROR;
    }

    if (from->size >= size) {
        return EINVAL;
    }

    if (from->vaddr == start) {
        // В начале
        res = apt_find_undef_entry(apt, new);
        if (res != OK) {
            return res;
        }

        new->vaddr = start;
        new->size = size;
        new->paddr = from->paddr;
        if (from->paddr != 0) {
            from->paddr += size;
        }
        from->vaddr += size;
        from->size -= size;
        new->flags = from->flags;
        new->status = from->status;
        new->cache = from->cache;

        if (from->prev != 0) {
            ((vm_abstract_pt_entry_t *) from->prev)->next = new;
            new->prev = from->prev;
            new->next = from;
            from->prev = new;
        } else {
            from->prev = new;
            new->next = first;
            table->first = new;
        }

    } else if (from->vaddr < start && from->vaddr + from->size == start + size) {
        // В конце
        res = apt_find_undef_entry(apt, new);
        if (res != OK) {
            return res;
        }

        new->vaddr = start;
        new->size = size;
        if (from->paddr != 0) {
            new->paddr = from->paddr + (from->size - size);
        } else {
            new->paddr = 0;
        }
        from->size -= size;
        new->flags = from->flags;
        new->status = from->status;
        new->cache = from->cache;

        if (from->next != 0) {
            ((vm_abstract_pt_entry_t *) from->next)->prev = new;
            new->next = from->next;
            new->prev = from;
            from->next = new;
        } else {
            from->next = new;
            new->prev = from;
            table->last = new;
        }

    } else {
        // По середине
        res = apt_find_undef_entry(apt, new);
        if (res != OK) {
            return res;
        }
        res = apt_find_undef_entry(apt, next);
        if (res != OK) {
            return res;
        }
        new->flags = from->flags;
        new->status = from->status;
        new->cache = from->cache;
        next->flags = from->flags;
        next->status = from->status;
        next->cache = from->cache;

        new->vaddr = start;
        new->size = size;
        if (from->paddr != 0) {
            new->paddr = from->paddr + (start - from->vaddr);
        } else {
            new->paddr = 0;
        }
        next->vaddr = start + size;
        next->size = (from->vaddr + from->size) - (start + size);
        if (from->paddr != 0) {
            next->paddr = new->paddr + size;
        } else {
            next->paddr = 0;
        }

        from->size = from->size - new->size - next->size;

        if (from->next != 0) {
            ((vm_abstract_pt_entry_t *) from->next)->prev = next;
            next->next = from->next;
        } else {
            next->next = 0;
            table->last = next;
        }

        from->next = new;
        new->prev = from;
        new->next = next;
        next->prev = new;
    }

    return OK;
}


/*
 * Общая функция для проверки свободного места в виртуальной памяти
 * При помощи этой функции мы будем защищать другие функции разметки
 */
int apt_check_free_memory_addr (vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes start, vir_bytes size) {
    vm_abstract_pt_entry_t  *iter;
    int res;

    for (res = apt_find_entry_by_virt_addr(table, start, iter); iter != 0 ; iter = (vm_abstract_pt_entry_t *) iter->next) {
        if (iter->status == VM_RECORD_FREE) {
           if (iter->size >= size) {
               return OK;
           } else {
               size -= iter->size;
           }
        } else {
          return APT_NOT_FREE;
        }
    }

    return APT_NOT_FREE;
}

/*
 * Размапить адреса физической памяти по виртуальному адресу
 * Функция проверяет размер и выделяет виртуальную память по запрошенному адресу
 * Адрес и размер должны быть выровнеными по границе page size
 */
int apt_map_phys_to_vir(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, phys_bytes addr, vir_bytes size,
                        vir_bytes start, vm_apt_flags_t flags, mmap_cache_hint_t cache_hint) {
    int res;
    vm_abstract_pt_entry_t *working_region = 0;
    vm_abstract_pt_entry_t *iter;


    res = apt_check_free_memory_addr(apt, table, start, size);
    if (res != OK) {
        return res;
    }

    if (size % apt->page_size || start % apt->page_size || addr % apt->page_size || size == 0) {
        return APT_ALIGNMENT_ERROR;
    }

    for (res = apt_find_entry_by_virt_addr(table, start, iter); iter != 0; iter = (vm_abstract_pt_entry_t *) iter->next) {
        if (iter->size == size) {
            if (working_region == 0) {
                working_region = iter;
            } else {
                size -= iter->size;
                concat(table, working_region, iter);
            }
        } else if (iter->size < size) {
            if (working_region == 0) {
                size -= iter->size;
                working_region = iter;
            } else {
                size -= iter->size;
                concat(table, working_region, iter);
                iter = working_region;
            }
        } else {
            if (working_region == 0) {
                biteoff(apt, table, start, size, iter, working_region);
                size = 0;
            } else {
                vm_abstract_pt_entry_t *tmp;
                biteoff(apt, table, working_region->vaddr + working_region->size, size, iter, tmp);
                concat(table, working_region, tmp);
                size = 0;
            }
        }
        if (size == 0) {
            working_region->status = VM_RECORD_INUSE;
            working_region->flags = flags;
            working_region->cache = cache_hint;
            working_region->paddr = addr;
            table->version++;
            return OK;
        }
    }

    return APT_NOT_FREE;
}

/*
 * Размапить регион карты памяти по виртуальному адресу
 * Функция проверяет размер и выделяет виртуальную память по запрошенному адресу
 * Адрес и размер должны быть выровнеными по границе страницы
 */
int apt_map_region_to_addr(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, mmap_region_t *region,
                   vir_bytes start, vm_apt_flags_t flags) {
    return apt_map_phys_to_vir(apt, table, region->start, region->size, start, flags, region->cache_hint);
}


/*
 * Размапить адреса физической памяти от мксимального свободного адреса сконца
 * Функция проверяет размер и выделяет виртуальную память по максимальному адресу сконца с учётом размера региона
 * start - результат, младший адрес непрерывного пространства размером size, куда мы размапили addr
 */
int apt_map_phys_to_vir_max_free_end(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, phys_bytes addr, phys_bytes size,
                        vir_bytes *start, vm_apt_flags_t flags, mmap_cache_hint_t cache_hint) {
    vm_abstract_pt_entry_t *iter;
    vir_bytes st = 0;
    vir_bytes sz = size;

    if (size % apt->page_size || size == 0) {
        return APT_ALIGNMENT_ERROR;
    }


    for (iter = table->last; iter != 0; iter = (vm_abstract_pt_entry_t *) iter->prev) {
        if (iter->status == VM_RECORD_FREE) {
            if (iter->size == sz) {
                st = iter->vaddr;
                sz = 0;
            } else if (iter->size < sz) {
                st = iter->vaddr;
                sz -= iter->size;
            } else {
                st = iter->vaddr + iter->size - sz;
                sz = 0;
            }
        } else {
            st = 0;
            sz = size;
        }

        if (sz == 0) {
            *start = st;
            break;
        }
    }

    if (sz != 0) {
        return APT_NOT_FREE;
    }

    return apt_map_phys_to_vir(apt, table, addr, size, st, flags, cache_hint);
}


/*
 * Размапить адреса физической памяти от минимально свободного адреса сначала
 * Функция проверяет размер и выделяет виртуальную память по минимальному адресу сначала с учётом размера региона
 * start - результат, младший адрес непрерывного пространства размером size, куда мы размапили addr
 */
int apt_map_phys_to_vir_min_free_start(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, phys_bytes addr, phys_bytes size,
                                     vir_bytes *start, vm_apt_flags_t flags, mmap_cache_hint_t cache_hint) {

    vm_abstract_pt_entry_t *iter;
    vir_bytes st = 0;
    vir_bytes sz = size;

    if (size % apt->page_size || size == 0) {
        return APT_ALIGNMENT_ERROR;
    }


    for (iter = table->first; iter != 0; iter = (vm_abstract_pt_entry_t *) iter->next) {
        if (iter->status == VM_RECORD_FREE) {
            if (iter->size == sz) {
                if (st == 0) {
                    st = iter->vaddr;
                }
                sz = 0;
            } else if (iter->size < sz) {
                if (st == 0) {
                    st = iter->vaddr;
                }
                sz -= iter->size;
            } else {
                if (st == 0) {
                    st = iter->vaddr;
                }
                sz = 0;
            }
        } else {
            st = 0;
            sz = size;
        }

        if (sz == 0) {
            *start = st;
            break;
        }
    }

    if (sz != 0) {
        return APT_NOT_FREE;
    }

    return apt_map_phys_to_vir(apt, table, addr, size, st, flags, cache_hint);
}

/*
 * Размапить регион из карты памяти от максимально свободного адреса сконца
 * Функция проверяет размер и выделяет виртуальную память по максимальному адресу с конца с учётом размера региона
 */
int apt_map_region_to_max_free_end(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, mmap_region_t *region,
                   vir_bytes *start, vm_apt_flags_t flags) {
    return apt_map_phys_to_vir_max_free_end(apt, table, region->start, start, region->size, flags, region->cache_hint);
}

/*
 * Размапить регион из карты памяти от минимально свободного адреса снизу
 * Функция проверяет размер и выделяет виртуальную память по минимальному адресу начала с учётом размера региона
 */
int apt_map_region_to_min_free_start(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, mmap_region_t *region,
                   vir_bytes *start, vm_apt_flags_t flags) {
    return apt_map_phys_to_vir_min_free_start(apt, table, region->start, start, region->size, flags, region->cache_hint);
}

/*
 * Размапить память для страниц выделяемых по требованию
 * Страницы по требованию у нас имеют ФЛАГ VM_APF_VIRTUAL_ONLY и физический адрес 0
 */
int apt_map_on_demand(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes start, vir_bytes size) {
    return apt_map_phys_to_vir(apt, table, 0, size, start, VM_APF_VIRTUAL_ONLY, 0);
}

/*
 * Освободить виртуальную память
 * Функция на автомате производит конкатенацию свободного пространства в целиковые регионы
 * Как всегда, адреса и размеры должны быть выровнены по apt->page_size
 * Но, кстати,этой функции похрену используется это пространство или нет, просто на выходе получится
 * Одно большое свободное пространство, если по краям у него тоже свободное пространство, оно сливается воедино
 * start и size могут попадать в середину записи
 */
int apt_unmap_vir_addr(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes start, vir_bytes size) {
    vm_abstract_pt_entry_t *iter;
    vm_abstract_pt_entry_t *working_region = 0;
    int res;

    if (start % apt->page_size || size % apt->page_size || size == 0) {
        return APT_ALIGNMENT_ERROR;
    }

    for (res = apt_find_entry_by_virt_addr(table, start, iter); iter != 0; iter = (vm_abstract_pt_entry_t *) iter->next) {
        if (iter->vaddr == start) {
            if (iter->size == size) {
               working_region = iter;
               size = 0;
            } else if (iter->size < size) {
               working_region = iter;
               size -= iter->size;
            } else {
               biteoff(apt, table, start, size, iter, working_region);
               size = 0;
            }
        } else if (iter->vaddr < start) {
            if (iter->vaddr + iter->size >= start + size) {
               biteoff(apt, table, start, size, iter, working_region);
               size = 0;
            } else {
                size -= iter->vaddr + iter->size - size;
                biteoff(apt, table, start, iter->vaddr + iter->size - size, iter, working_region);
                iter = working_region;
            }
        } else if (working_region != 0) {
            if (iter->size <= size) {
                size -= iter->size;
                concat(table, working_region, iter);
                iter = working_region;
            } else {
                vm_abstract_pt_entry_t *tmp;
                biteoff(apt, table, iter->vaddr, size, iter, tmp);
                concat(table, working_region, tmp);
                size = 0;
            }
        }

        if (size == 0) {
            working_region->status = VM_RECORD_FREE;
            working_region->flags = 0;
            working_region->cache = 0;
            working_region->paddr = 0;

            if (((vm_abstract_pt_entry_t *)working_region->prev)->status == VM_RECORD_FREE) {
                concat(table, (vm_abstract_pt_entry_t *)working_region->prev, working_region);
            }
            if (((vm_abstract_pt_entry_t *)working_region->next)->status == VM_RECORD_FREE) {
                concat(table, working_region, (vm_abstract_pt_entry_t *)working_region->next);
            }
            table->version++;
            return OK;
        }
    }

    return APT_NOT_FOUND;
}

/*
 * Освобождение всех страниц использующих физические адреса
 * РАЗМАПЛИВАЕТ ВСЕ ИСПОЛЬЗОВАНИЯ ЭТИХ АДРЕСОВ НЕЗАВИСИМО ОТ ИХ КОЛИЧЕСТВА
 */
int apt_unmap_phys_addr(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, phys_bytes start, phys_bytes size) {
    vm_abstract_pt_entry_t *working;
    int res;


    if (start % apt->page_size || size % apt->page_size || size == 0) {
        return APT_ALIGNMENT_ERROR;
    }

    while (apt_find_entry_by_phys_range(apt, start, start + size, working) == OK) {
        vir_bytes vstart, vend;
        if (working->paddr >= start) {
            vstart = working->vaddr;
        } else {
            vstart = working->vaddr + (start - working->paddr);
        }

        if (working->paddr + working->size <= start + size) {
            vend = working->vaddr + working->size;
        } else {
            vend = working->vaddr + size;
        }

        res = apt_unmap_vir_addr(apt, table, vstart, vend - vstart);
        if (res != OK) {
            return res;
        }
    }

    return OK;
}

/*
 * Удалить даные о регионе из таблицы страниц
 * Обёртка для apt_unmap_phys_addr(apt_unmap_vir_addr)
 * см. описание apt_unmap_phys_addr и apt_unmap_vir_addr для понимания механизма работы
 */
int apt_unmap_region(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, mmap_region_t *region) {
 return apt_unmap_phys_addr(apt, table, region->start, region->size);
}

/*
 * Изменить флаги для виртуальных адресов
 */
int apt_vir_set_new_flags(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes start, vir_bytes size, vm_apt_flags_t flags, mmap_cache_hint_t cache) {
    vm_abstract_pt_entry_t *iter;
    vm_abstract_pt_entry_t *working_region = 0;
    int res;

    if (start % apt->page_size || size % apt->page_size || size == 0) {
        return APT_ALIGNMENT_ERROR;
    }

    for (res = apt_find_entry_by_virt_addr(table, start, iter); iter != 0; iter = (vm_abstract_pt_entry_t *) iter->next) {
        if (iter->vaddr == start) {
            if (iter->size == size) {
                working_region = iter;
                size = 0;
            } else if (iter->size < size) {
                working_region = iter;
                size -= iter->size;
            } else {
                biteoff(apt, table, start, size, iter, working_region);
                size = 0;
            }
        } else if (iter->vaddr < start) {
            if (iter->vaddr + iter->size >= start + size) {
                biteoff(apt, table, start, size, iter, working_region);
                size = 0;
            } else {
                size -= iter->vaddr + iter->size - size;
                biteoff(apt, table, start, iter->vaddr + iter->size - size, iter, working_region);
                iter = working_region;
            }
        } else if (working_region != 0) {
            if (iter->size <= size) {
                size -= iter->size;
                concat(table, working_region, iter);
                iter = working_region;
            } else {
                vm_abstract_pt_entry_t *tmp;
                biteoff(apt, table, iter->vaddr, size, iter, tmp);
                concat(table, working_region, tmp);
                size = 0;
            }
        }

        if (size == 0) {
            working_region->flags = flags;
            working_region->cache = cache;

            table->version++;
            return OK;
        }
    }

    return APT_NOT_FOUND;
}

int apt_copy_table(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *source, vm_abstract_pt_t *dest) {
    vm_abstract_pt_entry_t *iter;
    vm_abstract_pt_entry_t *new_entry;

    apt_find_undef_table(apt, dest);
    dest->status = source->status;
    dest->version = 1;

    for (iter = source->first; iter != 0; iter = (vm_abstract_pt_entry_t *) iter->next) {
        apt_find_undef_entry(apt, new_entry);
        new_entry->size = iter->size;
        new_entry->vaddr = iter->vaddr;
        new_entry->paddr = iter->paddr;
        new_entry->status = iter->status;
        new_entry->flags = iter->flags;
        new_entry->cache = iter->cache;
        apt_hard_add_entry_last(dest, new_entry);
    }

    return OK;
}
