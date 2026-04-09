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
 * Возможно, так же станет и куском нового VM
 *
 * И ДА! ЭТО МНОГО ВЕТВЛЕНИЯ И ОПЕРАЦИЙ, НО ЭТО ЕДИНСТВЕННОЕ ЛЕКАРСТВО ДЛЯ СВОБОДНОЙ ПОДДЕРЖКИ МНОГИХ АРХИТЕКТУР
 *
 * Это как общий для всей системы язык описания виртуальной памяти, а mmap это общий язык для описания физической памяти
 *
 * НЕ ЗАБУДЬ СТАВИТЬ СПИНЛОКИ ПОСЛЕ ЗАПУСКА SMP
 *
 */

/*
 * Поиск таблицы по её владельцу
 */
int apt_find_table_by_endpoint(vm_abstract_pagetables_t *apt, endpoint_t endpoint, vm_abstract_pt_t *table) {
    vm_abstract_pt_t *iter;
    for (iter = apt->first_pagetable; iter->next != 0; iter = iter->next) {
        if (iter->owner == endpoint) {
            table = iter;
            return 1;
        }
    }
    return APT_NOT_FOUND;
}

/*
 * Поиск таблицы по её физическому адресу
 */
int apt_find_table_by_phys_addr(vm_abstract_pagetables_t *apt, phys_bytes addr, vm_abstract_pt_t *table) {
    vm_abstract_pt_t *iter;
    for (iter = apt->first_pagetable; iter->next != 0; iter = iter->next) {
        if (iter->phys_pt_root == addr) {
            table = iter;
            return 1;
        }
    }
    return APT_NOT_FOUND;
}

/*
 * Поиск первой незанятой записи о таблице
 */
static int apt_find_undef_table(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table) {
    if (apt->pagetables_allocated == apt->pagetables_used) {
        return APT_ERROR_NO_MORE_UNDEF_RECORDS;
    }

    for (int i = 0; i < apt->pagetables_allocated; i++) {
        if (apt->tables[i].status == VM_RECORD_UNDEF) {
            table = &apt->tables[i];
            table->status = VM_RECORD_INUSE;
            apt->pagetables_used++;
            return 1;
        }
    }

    return APT_DEFRAGMENTATION_ERROR;
}

/*
 *   поиск первой незанятой записи о l1 секции
 */
static int apt_find_undef_l1_entry(vm_abstract_pagetables_t *apt, vm_abstract_pt_l1_entry_t *l1_entry) {
    if (apt->l1_entries_allocated == apt->l1_entries_used) {
        return APT_ERROR_NO_MORE_UNDEF_RECORDS;
    }

    for (int i = 0; i < apt->l1_entries_allocated; i++) {
        if (apt->l1_entries[i].status == VM_RECORD_UNDEF) {
            l1_entry = &apt->l1_entries[i];
            l1_entry->status = VM_RECORD_FREE;
            apt->l1_entries_used++;
            return 1;
        }
    }

    return APT_DEFRAGMENTATION_ERROR;
}

/*
 * Поиск первой незанятой записи о странице l2
 */
static int apt_find_undef_l2_entry(vm_abstract_pagetables_t *apt, vm_abstract_pt_l2_entry_t *l2_entry) {
    if (apt->l2_entries_allocated == apt->l2_entries_used) {
        return APT_ERROR_NO_MORE_UNDEF_RECORDS;
    }

    for (int i = 0; i < apt->l2_entries_allocated; i++) {
        if (apt->l2_entries[i].status == VM_RECORD_UNDEF) {
            l2_entry = &apt->l2_entries[i];
            l2_entry->status = VM_RECORD_FREE;
            apt->l2_entries_used++;
            return 1;
        }
    }

    return APT_DEFRAGMENTATION_ERROR;
}

/*
 * Физическое исключение записи о l2 странице из таблицы страниц
 * Низкоуровневая функция, используй функции более высокого уровня для переразметки памяти
 */
static int apt_unmap_l2_entry(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table,
                       vm_abstract_pt_l1_entry_t *l1_entry, vm_abstract_pt_l2_entry_t *l2_entry) {
    if (l2_entry->next != 0) {
        ((vm_abstract_pt_l2_entry_t *) l2_entry->next)->prev = l2_entry->prev;
    } else {
        l1_entry->last_l2_entry = l2_entry->prev;
        ((vm_abstract_pt_l2_entry_t *)l2_entry->prev)->next = (void *)0;
    }

    if (l2_entry->prev != 0) {
        ((vm_abstract_pt_l2_entry_t *) l2_entry->prev)->next = l2_entry->next;
    } else {
        l1_entry->first_l2_entry = l2_entry->next;
        ((vm_abstract_pt_l2_entry_t *)l2_entry->next)->prev = (void *)0;
    }

    memset((void *)l2_entry, 0, sizeof (vm_abstract_pt_l2_entry_t));
    apt->l2_entries_used--;

    return 1;
}

/*
 * Физическое исключение записи о l1 секции из таблицы страниц
 * Низкоуровневая функция, используй функции более высокого уровня для переразметки памяти
 */
static int apt_unmap_l1_entry(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vm_abstract_pt_l1_entry_t *l1_entry) {
    vm_abstract_pt_l2_entry_t *iter;

    if (l1_entry->next != 0) {
        ((vm_abstract_pt_l1_entry_t *)l1_entry->next)->prev = l1_entry->prev;
    } else {
        table->last_entry = l1_entry->prev;
        ((vm_abstract_pt_l1_entry_t *)l1_entry->prev)->next = (void *)0;
    }

    if (l1_entry->prev != 0) {
        ((vm_abstract_pt_l1_entry_t *)l1_entry->prev)->next = l1_entry->next;
    } else {
        table->first_entry = l1_entry->next;
        ((vm_abstract_pt_l1_entry_t *)l1_entry->next)->prev = (void *)0;
    }

    if (l1_entry->type == VM_APT_L1_L2PT) {
        for (iter = l1_entry->first_l2_entry; iter != 0; iter = (vm_abstract_pt_l2_entry_t *) iter->next) {
            apt_unmap_l2_entry(apt, table, l1_entry, iter);
        }
    }
    memset(l1_entry, 0, sizeof(vm_abstract_pt_l1_entry_t));


    return 1;
}

/*
 * Удаление таблицы страниц, например в связи с преркращением процесса,
 * И возврат их записей в пулл свободных
 */
int apt_unmap_table(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table) {
    vm_abstract_pt_l1_entry_t *iter;
    if (table->next != 0) {
        ((vm_abstract_pt_t *)table->next)->prev = table->prev;
    } else {
        apt->last_pagetable = table->prev;
        ((vm_abstract_pt_t *)table->prev)->next = (void *)0;
    }

    if (table->prev != 0) {
        ((vm_abstract_pt_t *)table->prev)->next = table->next;
    } else {
        apt->first_pagetable = table->next;
        ((vm_abstract_pt_t *)table->next)->prev = (void *)0;
    }

    for (iter = table->first_entry; iter != 0; iter = iter->next) {
        apt_unmap_l1_entry(apt, table, iter);
    }

    memset(table, 0, sizeof(vm_abstract_pt_t));

    apt->pagetables_used--;

    return 1;
}

/*
 * Поиск l1 секции в таблице страниц по виртуальному адресу
 */
int apt_find_l1_entry_by_virt_addr(vm_abstract_pt_t *table, vir_bytes addr, vm_abstract_pt_l1_entry_t *l1_entry) {
    vm_abstract_pt_l1_entry_t *iter;

    for (iter = table->first_entry; iter != 0; iter = (vm_abstract_pt_l1_entry_t *)iter->next) {
        if (addr >= iter->vaddr  && iter->vaddr + iter->size > addr) {
            l1_entry = iter;
            return 1;
        }
    }
    return APT_NOT_FOUND;
}

/*
 * Поиск l1 секции в таблице страниц по физическому адресу
 */
int apt_find_l1_entry_by_phys_addr(vm_abstract_pt_t *table, phys_bytes addr, vm_abstract_pt_l1_entry_t *l1_entry) {
    vm_abstract_pt_l1_entry_t *iter;

    for (iter = table->first_entry; iter != 0; iter = (vm_abstract_pt_l1_entry_t *)iter->next) {
        if (addr >= iter->paddr  && iter->paddr + iter->size > addr) {
            l1_entry = iter;
            return 1;
        }
    }
    return APT_NOT_FOUND;
}

/*
 * Поиск l2 страницы в l1 секции по виртуальному адресу
 */
int apt_find_l2_entry_by_virt_addr(vm_abstract_pt_l1_entry_t *l1_entry, vir_bytes addr, vm_abstract_pt_l2_entry_t *l2_entry) {
    vm_abstract_pt_l2_entry_t *iter;

    for (iter = l1_entry->first_l2_entry; iter != 0; iter = (vm_abstract_pt_l2_entry_t *)iter->next) {
        if (addr >= iter->vaddr  && iter->vaddr + iter->size > addr) {
            l2_entry = iter;
            return 1;
        }
    }
    return APT_NOT_FOUND;
}

/*
 * Поиск l2 страницы в l1 секции по физическому адресу
 */
int apt_find_l2_entry_by_phys_addr(vm_abstract_pt_l1_entry_t *l1_entry, phys_bytes addr, vm_abstract_pt_l2_entry_t *l2_entry) {
    vm_abstract_pt_l2_entry_t *iter;

    for (iter = l1_entry->first_l2_entry; iter != 0; iter = (vm_abstract_pt_l2_entry_t *)iter->next) {
        if (addr >= iter->paddr  && iter->paddr + iter->size > addr) {
            l2_entry = iter;
            return 1;
        }
    }
    return APT_NOT_FOUND;
}

/*
 * Размапить полностью пустую новую таблицу памяти
 */
int apt_make_clean_table(vm_abstract_pagetables_t *apt, endpoint_t owner, vm_abstract_pt_t *new_table) {
    int res = apt_find_undef_table(apt, new_table);
    if (res < 0) {
        return res;
    }
    new_table->owner = owner;
    if (apt->last_pagetable == 0) {
        apt->first_pagetable = new_table;
        apt->last_pagetable = new_table;
    } else {
        apt->last_pagetable->next = (void *) new_table;
        new_table->prev = (void *) apt->last_pagetable;
        apt->last_pagetable = new_table;
    }
    vm_abstract_pt_l1_entry_t *new_l1_entry;
    res = apt_find_undef_l1_entry(apt, new_l1_entry);
    if (res < 0) {
        apt_unmap_table(apt, new_table);
        return res;
    }
    new_l1_entry->type = VM_APT_L1_SECTION;
    new_l1_entry->status = VM_RECORD_FREE;
    new_l1_entry->vaddr = 0x0;
    new_l1_entry->size = apt->l1_section_size * apt->l1_sections_max_count;
    new_l1_entry->dirty = 1;
    new_table->entries_dirty++;
    new_table->entries_count++;
    new_table->first_entry = new_l1_entry;
    new_table->last_entry = new_l1_entry;
    new_table->version = 1;

    return 1;
}

/*
 *  Инициализация таблицы l2 внутри секции l1
 *  Ничего сложного, но вынесена в отдельную функцию что бы не писать код по 200 раз
 *  l1_entry - указатель на уже выделенную внутри таблицы страниц секцию l1 размером с apt->l1_section_size
 */
static inline int init_free_l2_section(vm_abstract_pagetables_t *apt, vm_abstract_pt_l1_entry_t *l1_entry) {
    vm_abstract_pt_l2_entry_t *new_l2_entry;
    int res;

    l1_entry->status = VM_RECORD_INUSE;
    l1_entry->type = VM_APT_L1_L2PT;

    res = apt_find_undef_l2_entry(apt, new_l2_entry);
    if (res <= 0) {
        return res;
    }

    new_l2_entry->status = VM_RECORD_FREE;
    new_l2_entry->flags = l1_entry->flags;
    new_l2_entry->cache_hint = l1_entry->cache_hint;
    new_l2_entry->size = apt->l1_section_size;
    new_l2_entry->vaddr = l1_entry->vaddr;
    new_l2_entry->paddr = l1_entry->paddr;
    l1_entry->first_l2_entry = new_l2_entry;
    l1_entry->last_l2_entry = new_l2_entry;
    l1_entry->l2_entries_count = 1;
    l1_entry->dirty = 1;

    return 1;
}

/*
 * Объединение соседних записей о страницах l2 внутри секции l1
 * Необходимо соблюдать порядок first - младшие адреса, second старшие
 * Новая запись наследует всё от first, указатель на получившуся запись так же будет в first
 * Данной функции похуй на paddr и vaddr - просто увеличится размер first
 * ЗДЕСЬ НЕТ ВЕРСИОНИРОВАНИЯ
 */
static inline int concat_l2 (vm_abstract_pagetables_t *apt, vm_abstract_pt_l1_entry_t *l1_entry,
                             vm_abstract_pt_l2_entry_t *first, vm_abstract_pt_l2_entry_t *second) {
    if (l1_entry->last_l2_entry == second) {
        l1_entry->last_l2_entry = first;
    }
    l1_entry->l2_entries_count--;
    l1_entry->dirty = 1;
    first->size += second->size;
    if (second->next != 0) {
        ((vm_abstract_pt_l2_entry_t *)second->next)->prev = (void *) first;
    }
    first->next = second->next;
    memset((void *)second, 0, sizeof(vm_abstract_pt_l2_entry_t));
    return 1;
}


/*
 * Объединение соседних записей о l1 секциях - ОНИ НЕ ДОЛЖНЫ БЫТЬ ТАБЛИЦАМИ L2
 * Необходимо строго соблюдать порядок first - указатель на младшие адреса, second на старшие
 * Новая запись наследует всё от first, указатель на получившуся запись так же будет в first
 * Данной функции похуй на paddr и vaddr - просто увеличится размер first
 * ЗДЕСЬ НЕТ ВЕРСИОНИРОВАНИЯ
 */
static inline int concat_l1 (vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table,
                             vm_abstract_pt_l1_entry_t *first, vm_abstract_pt_l1_entry_t *second) {
    if (table->last_entry == second) {
        table->last_entry = first;
    }
    table->entries_count--;
    first->size += second->size;
    if (second->next != 0) {
        ((vm_abstract_pt_l1_entry_t *)second->next)->prev = (void *) first;
    }
    first->next = second->next;
    memset((void *) second, 0, sizeof(vm_abstract_pt_l1_entry_t));
    return 1;
}

/*
 * Превратить таблицу l2 страниц в обычную секцию l1 обнулив и вернув в пул все записи о l2 страницах
 * Оставляет все флаги из l1 записи и также кладёт хуй на адреса в l2
 * Физический адрес берёт из первой записи l2
 * ЗДЕСЬ НЕТ ВЕРСИОНИРОВАНИЯ
 */
static inline int convert_l2pt_to_l1_section(vm_abstract_pagetables_t *apt, vm_abstract_pt_l1_entry_t *entry) {
    vm_abstract_pt_l2_entry_t *iter;
    if (entry->type != VM_APT_L1_L2PT) {
        return 1;
    }
    entry->type = VM_APT_L1_SECTION;
    entry->dirty = 1;
    entry->paddr = entry->first_l2_entry->paddr;
    for (iter = entry->first_l2_entry; iter != 0; iter = (vm_abstract_pt_l2_entry_t *) iter->next) {
        memset((void *)iter, 0, sizeof(vm_abstract_pt_l2_entry_t));
    }
    entry->l2_entries_count = 0;
    entry->first_l2_entry = 0;
    entry->last_l2_entry = 0;
    return 1;
}


/*
 * Откусить отдельную запись для новой l2 страницы записи побольше
 * Новая запись унаследует все флаги и состояния из родительской
 * Адреса, размеры и т.д. как всегда должны быть выровнены по apt->l2_page_size
 */
static inline int biteoff_l2 (vm_abstract_pagetables_t *apt, vm_abstract_pt_l1_entry_t *l1_entry, vir_bytes start, vir_bytes size,
                              vm_abstract_pt_l2_entry_t *from, vm_abstract_pt_l2_entry_t *new_l2) {
    int res;
    // ну так проверим на всякий случай, хотя предполагаем что так не должно быть, но вдруг я где-то забудусь
    if (size > from->size) {
        return APT_NOT_FREE;
    }
    if (from->vaddr > start || (from->vaddr + from->size) < start + size) {
        return APT_NOT_FREE;
    }
    if (start % apt->l2_page_size > 0 || size % apt->l2_page_size) {
        return APT_ALIGNMENT_ERROR;
    }

    if (from->vaddr == start && size == from->size) {
        new_l2 = from;
        return 1;
    }

    if (start == from->vaddr) {
        // Наш кусок вначале
        res = apt_find_undef_l2_entry(apt, new_l2);
        if (res <= 0) {
            return res;
        }
        new_l2->paddr = from->paddr;
        new_l2->size = size;
        new_l2->vaddr = start;
        new_l2->flags = from->flags;
        new_l2->cache_hint = from->cache_hint;
        new_l2->status = from->status;
        from->vaddr += size;
        if (from->paddr != 0) {
            from->paddr += size;
        }
        from->size -= size;
        if (l1_entry->first_l2_entry == from) {
            l1_entry->first_l2_entry = new_l2;
        }
        new_l2->prev = from->prev;
        new_l2->next = (void *) from;
        from->prev = (void *) new_l2;
        l1_entry->l2_entries_count++;
    } else if (start + size == from->vaddr + from->size) {
        // Наш кусок вконце
        res = apt_find_undef_l2_entry(apt, new_l2);
        if (res <= 0) {
            return res;
        }
        if (from->paddr != 0) {
            new_l2->paddr = from->paddr + (start - from->vaddr);
        } else {
            new_l2->paddr = 0;
        }
        new_l2->size = size;
        new_l2->vaddr = start;
        new_l2->flags = from->flags;
        new_l2->cache_hint = from->cache_hint;
        new_l2->status = from->status;
        from->size -= size;
        if (l1_entry->last_l2_entry == from) {
            l1_entry->last_l2_entry = new_l2;
        }
        new_l2->prev = (void *) from;
        if (from->next != 0) {
            ((vm_abstract_pt_l2_entry_t *)from->next)->prev = new_l2;
        }
        new_l2->next = from->next;
        from->next = (void *) new_l2;
        l1_entry->l2_entries_count++;
    } else {
        // Мы в середине
        vm_abstract_pt_l2_entry_t *next_l2;
        res = apt_find_undef_l2_entry(apt, new_l2);
        if (res <= 0) {
            return res;
        }

        res = apt_find_undef_l2_entry(apt, next_l2);
        if (res <= 0) {
            return res;
        }
        new_l2->vaddr = start;
        new_l2->size = size;
        new_l2->flags = from->flags;
        new_l2->cache_hint = from->cache_hint;
        new_l2->status = from->status;
        next_l2->vaddr = start + size;
        next_l2->size = from->size - (start - from->vaddr) - size;
        next_l2->flags = from->flags;
        next_l2->cache_hint = from->cache_hint;
        next_l2->status = from->status;
        from->size = start - from->vaddr;
        next_l2->next = from->next;
        if (from->next != 0) {
            ((vm_abstract_pt_l2_entry_t *) from->next)->prev = (void *) next_l2;
        }
        if (l1_entry->last_l2_entry == from) {
            l1_entry->last_l2_entry = next_l2;
        }
        new_l2->next = (void *) next_l2;
        new_l2->prev = (void *) from;
        next_l2->prev = (void *) new_l2;
        from->next = (void *) new_l2;
        l1_entry->l2_entries_count += 2;
    }
    l1_entry->dirty = 1;
    return 1;
}

/*
 * Откусить или выгрызть новую секцию из большой секции l1
 * указатель на полученный новый кусок в последней переменной
 * Новая запись унаследует все флаги и состояния из родительской
 * Адреса, размеры и т.д. как всегда должны быть выровнены по apt->l1_section_size
 * ВНИМАНИЕ НЕ ПРИМЕНЯТЬ НА СЕКЦИЯХ L2PT
 */
static inline int biteoff_l1 (vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes start, vir_bytes size,
                              vm_abstract_pt_l1_entry_t *from, vm_abstract_pt_l1_entry_t *new_l1) {
    // Сделал inline что бы меньше прыгать по адресам и стэку и так тут пиздец творится
    int res;
    // Как всегда проверки, на случай если я буду страдать деменцией
    if (size > from->size) {
        return APT_NOT_FREE;
    }
    if (from->vaddr > start || (from->vaddr + from->size) < start + size) {
        return APT_NOT_FREE;
    }
    if (start % apt->l1_section_size > 0 || size % apt->l1_section_size) {
        return APT_ALIGNMENT_ERROR;
    }

    if (from->vaddr == start && size == from->size) {
        new_l2 = from;
        return 1;
    }

    if (from->vaddr == start) {
        // мы в начале
        res = apt_find_undef_l1_entry(apt, new_l1);
        if (res <= 0) {
            return res;
        }
        new_l1->type = from->type;
        new_l1->status = from->status;
        new_l1->vaddr = start;
        new_l1->size = size;
        new_l1->flags = from->flags;
        new_l1->cache_hint = from->cache_hint;
        new_l1->dirty = 1;
        from->dirty = 1;
        table->entries_dirty += 2;
        from->size -= size;
        from->vaddr += size;
        new_l1->paddr = from->paddr;
        if (from->paddr != 0) {
            from->paddr += size;
        }
        if (table->first_entry == from) {
            table->first_entry = from;
        }
        if (from->prev != 0) {
            ((vm_abstract_pt_l1_entry_t *) from->prev)->next = (void *) new_l1;
        }
        new_l1->prev = from->prev;
        new_l1->next = (void *) from;
        from->prev = (void *) new_l1;
        table->entries_count++;
    } else if (from->vaddr + from->size == start + size) {
        // Мы в конце
        res = apt_find_undef_l1_entry(apt, new_l1);
        if (res <= 0) {
            return res;
        }
        new_l1->type = from->type;
        new_l1->status = from->status;
        new_l1->vaddr = start;
        new_l1->size = size;
        new_l1->flags = from->flags;
        new_l1->cache_hint = from->cache_hint;
        new_l1->dirty = 1;
        from->dirty = 1;
        table->entries_dirty += 2;
        from->size -= size;
        new_l1->paddr = from->paddr;
        if (from->paddr != 0) {
            new_l1->paddr += size;
        }
        if (table->last_entry == from) {
            table->last_entry = new_l1;
        }
        if (from->next != 0) {
            ((vm_abstract_pt_l1_entry_t *) from->next)->prev = (void *) new_l1;
        }
        new_l1->next = from->next;
        from->next = (void *) new_l1;
        new_l2->prev = (void *) from;
        table->entries_count++;
    } else {
        // Мы в середине
        vm_abstract_pt_l1_entry_t *next_l1;
        res = apt_find_undef_l1_entry(apt, new_l1);
        if (res <= 0) {
            return res;
        }
        res = apt_find_undef_l1_entry(apt, next_l1);
        if (res <= 0) {
            return res;
        }
        new_l1->type = from->type;
        new_l1->status = from->status;
        new_l1->vaddr = start;
        new_l1->size = size;
        new_l1->flags = from->flags;
        new_l1->cache_hint = from->cache_hint;
        new_l1->dirty = 1;
        next_l1->type = from->type;
        next_l1->status = from->status;
        next_l1->vaddr = start + size;
        next_l1->size = from->size - (start - from->vaddr) - size;
        next_l1->flags = from->flags;
        next_l1->cache_hint = from->cache_hint;
        next_l1->dirty = 1;
        from->dirty = 1;
        from->size = start - from->vaddr;
        next_l1->next = from->next;
        if (table->last_entry == from) {
            table->last_entry = next_l1;
        }
        if (from->next != 0) {
            ((vm_abstract_pt_l1_entry_t *) from->next)->prev = (void *) next_l1;
        }
        from->next = (void *) new_l1;
        new_l1->prev = (void *) from;
        new_l1->next = (void *) next_l1;
        next_l1->prev = (void *) new_l1;
        table->entries_count++;
        table->entries_dirty++;
    }

    return 1;
}

/*
 * Откусывание одной таблицы страниц l2 из крупной секции l1
 * указатель на полученный новый кусок в последней переменной
 */
static inline int biteoff_l2pt_from_l1 (vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes start,
                                     vm_abstract_pt_l1_entry_t *from, vm_abstract_pt_l1_entry_t *l2pt_slice) {
    int res;
    res = biteoff_l1(apt, table, start, apt->l1_section_size, from, l2pt_slice);
    if (res <= 0) {
        return res;
    }
    res = init_free_l2_section(apt, l2pt_slice);
    return res;
}

/*
 * Общая функция для проверки свободного места в виртуальной памяти
 * При помощи этой функции мы будем защищать другие функции разметки
 */
int apt_check_free_memory_addr (vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes start, vir_bytes size) {
    vm_abstract_pt_l1_entry_t *working_l1;
    vm_abstract_pt_l2_entry_t *working_l2;
    int res;

    for (res = apt_find_l1_entry_by_virt_addr(table, start, working_l1);
            working_l1 != 0;
            working_l1 = (vm_abstract_pt_l1_entry_t *) working_l1->next) {
         if (res < 0) {
             return 0;
         }
         if (working_l1->status == VM_RECORD_FREE) {
            //  Мы в свободной секции
            if (working_l1->size + working_l1->vaddr >= start + size) {
                // мы уместились в секции
                return 1;
            } else {
                // Осталось проверить уже меньше
                size = (start + size) - (working_l1->size + working_l1->vaddr);
                if (working_l1->next == 0) {
                    // у нас нет записи о следующей записи l1
                    // так что мы даже не будем разбираться а просто скажем, что не поместились
                    return 0;
                }
                start = ((vm_abstract_pt_l1_entry_t *) working_l1->next)->vaddr;  // начнём с адреса следующей записи
            }
         } else if (working_l1->type == VM_APT_L1_L2PT) {
             // Мы попали в секцию l2, придётся её перебрать
             if (start % apt->l2_page_size > 0) {
                 // выровняем старт по размеру страницы l2
                 // так как нам интересно всё уместить, то выровняем вниз
                 // но на будущее нужно понимать, что мои функции этой библиотеки не всегда выровняют страницы,
                 // нужно заранее выравнивать данные перед разметкой
                 start = start - (start % apt->l2_page_size);
             }

             for (res = apt_find_l2_entry_by_virt_addr(working_l1, start, working_l2);
                    working_l2 != 0;
                    working_l2 = (vm_abstract_pt_l2_entry_t *) working_l2->next) {
                    if (res < 0) {
                        return 0;
                    }
                    if (working_l2->status == VM_RECORD_FREE) {
                        if (working_l2->start + apt->l2_page_size >= start + size) {
                            // Мы уместились в этой записи о страницах
                            return 1;
                        } else {
                            // У нас осталось ещё не размеченное пространство
                            size = (start + size) - (working_l2->vaddr + apt->l2_page_size);
                            if (working_l2->next == 0) {
                                // мы упёрлись в конец секции
                                if (working_l1->next == 0) {
                                    // у нас нет записи о следующей записи l1
                                    // так что мы даже не будем разбираться а просто скажем, что не поместились
                                    return 0;
                                }
                                start = ((vm_abstract_pt_l1_entry_t *) working_l1->next)->vaddr;  // начнём с адреса следующей записи о секции l1
                            } else {
                              start = ((vm_abstract_pt_l2_entry_t *) working_l2->next)->vaddr; // пересчёлкиваем адрес на адрес следующей записи l2
                            }
                        }
                    } else {
                        return 0;
                    }
             }
         } else {
             return 0;
         }
    }
    return 0;
}

/*
 * Размапить адреса физической памяти по виртуальному адресу
 * Функция проверяет размер и выделяет виртуальную память по запрошенному адресу
 * Адрес и размер должны быть выровнеными по границе l2
 */
int apt_map_phys_to_vir(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, phys_bytes addr, vir_bytes size,
                        vir_bytes start, vm_apt_flags_t flags, mmap_cache_hint_t cache_hint) {
    vm_abstract_pt_l1_entry_t *working_l1;
    vm_abstract_pt_l2_entry_t *working_l2;
    int res;

    // Проверим выравнивание по границам l2
    if ((start % apt->l2_page_size > 0) || ((addr % apt->l2_page_size > 0) || (size % apt->l2_page_size > 0))) {
        return APT_ALIGNMENT_ERROR;
    }

    // Проверим наличие свободной памяти
    if (!apt_check_free_memory_addr(apt, table, start, size)) {
        return APT_NOT_FREE;
    }

    // Размечать как всегда будем циклом
    for (res = apt_find_l1_entry_by_virt_addr(table, start, working_l1);
         working_l1 != 0;
         working_l1 = (vm_abstract_pt_l1_entry_t *) working_l1->next) {
        if (res <= 0) {
            return res;
        }
        if (working_l1->status == VM_RECORD_INUSE && working_l1->type == VM_APT_L1_L2PT) {
            // Мы получили регион который размечен как таблица страниц l2
            // Это случилось потому что часть размечаемой памяти попала вконце этого региона
            vm_abstract_pt_l2_entry_t *new_l2;
            res = biteoff_l2(apt, working_l1, start, working_l1->vaddr + apt->l1_section_size - start, working_l1->last_l2_entry, new_l2);
            if (addr != 0)
            new_l2->paddr = addr;

            new_l2->flags = flags;
            new_l2->cache_hint = cache_hint;
            new_l2->status = VM_RECORD_INUSE;
            start += new_l2->size;

            if (addr != 0)
            addr += new_l2->size;

            size -= new_l2->size;
            if (size > 0) {
                continue; // Разметив начало нашей памяти в l2pt летим на следующую итерацию
            }
            // Ну а если мы всё уместили, то можем уходить.
            table->version++;
            return 1;
        } else if (working_l1->status != VM_RECORD_FREE) {
            return APT_DEFRAGMENTATION_ERROR;
        }


        if (working_l1->vaddr == start) {
            // Мы в начале l1 записи
            if (working_l1->size == size) {
               // Совпало!
               working_l1->status = VM_RECORD_INUSE;
               if (working_l1->type == VM_APT_L1_L2PT) {
                   res = convert_l2pt_to_l1_section(apt, working_l1);
                   if (res <= 0) {
                       return res;
                   }
               }
               if (addr != 0)
               working_l1->paddr = addr;

               working_l1->flags = flags;
               working_l1->cache_hint = cache_hint;
               table->version++;
               return 1;
            } else if (size < working_l1->size) {
                // Мы умещаемся целиком в этот регион
                if (size % apt->l1_section_size == 0) {
                    // Регион отлично выровнен по размеру l1
                    vm_abstract_pt_l1_entry_t *new_l1;
                    res = biteoff_l1(apt, table, start, size, working_l1, new_l1);
                    if (res <= 0) {
                        return res;
                    }
                    if (addr != 0)
                    new_l1->paddr = addr;

                    new_l1->status = VM_RECORD_INUSE;
                    new_l1->flags = flags;
                    new_l1->cache_hint = cache_hint;
                    table->version++;
                    return 1;
                } else {
                    // У нас остаётся кусок, который нужно разметить как l2
                    vir_bytes l2_size = size % apt->l1_section_size;
                    vir_bytes l1_size = size - l2_size;
                    vm_abstract_pt_l1_entry_t *new_l1;
                    vm_abstract_pt_l1_entry_t *new_l2pt;
                    res = biteoff_l1(apt, table, start, size, working_l1, new_l1);
                    if (res <= 0) {
                        return res;
                    }
                    res = biteoff_l2pt_from_l1(apt, table, start + l1_size, working_l1, new_l2pt);
                    if (res <= 0) {
                        return res;
                    }
                    vm_abstract_pt_l2_entry_t *new_l2;
                    res = biteoff_l2(apt, new_l2pt, new_l2pt->vaddr, l2_size, new_l2pt->first_l2_entry, new_l2);
                    new_l1->paddr = addr;
                    if (addr != 0) {
                        new_l2pt->paddr = addr + l1_size;
                        new_l2->paddr = addr + l1_size;
                    }

                    new_l1->status = VM_RECORD_INUSE;
                    new_l1->flags = flags;
                    new_l1->cache_hint = cache_hint;
                    new_l2->status = VM_RECORD_INUSE;
                    new_l2->flags = flags;
                    new_l2->cache_hint = cache_hint;
                    new_l2pt->status = VM_RECORD_INUSE;
                    new_l2pt->flags = flags;
                    new_l2pt->cache_hint = cache_hint;
                    table->version++;
                    return 1;
                }
            } else {
                if (addr != 0)
               working_l1->paddr = addr;
               working_l1->status = VM_RECORD_INUSE;
               working_l1->flags = flags;
               working_l1->cache_hint = cache_hint;
               start += working_l1->size;

                if (addr != 0)
               addr += working_l1->size;

               size -= working_l1->size;
               // летим размечать следующий кусок
            }
        } else {
            // Мы в середине
            if (working_l1->vaddr + working_l1->size >= start + size) {
                // Помещаемся в эту запись
                if (start % apt->l1_section_size > 0) {
                    // Начало внутри l2pt
                    vm_abstract_pt_l1_entry_t *new_l2pt;
                    vm_abstract_pt_l1_entry_t *new_l1;
                    vm_abstract_pt_l2_entry_t *new_l2;
                    res = biteoff_l2pt_from_l1(apt, table, start - (start % apt->l1_section_size), working_l1, new_l2pt);
                    if (res <= 0) {
                        return res;
                    }
                    new_l2pt->flags = flags;
                    new_l2pt->cache_hint = cache_hint;

                    if (new_l2pt->vaddr + apt->l1_section_size < start + size) {
                        // Мы умещаемся в l2pt
                        res = biteoff_l2(apt, new_l2pt, start, size, new_l2pt->first_l2_entry, new_l2);
                        if (res <= 0) {
                            return res;
                        }
                        if (addr != 0)
                        new_l2->paddr = addr;
                        new_l2->flags = flags;
                        new_l2->status = VM_RECORD_INUSE;
                        new_l2->cache_hint = cache_hint;
                        table->version++;
                        return 1;
                    }
                    // Мы не помещаемся в l2_section_size
                    res = biteoff_l2(apt, new_l2pt, start, new_l2pt->vaddr + apt->l1_section_size - start, new_l2pt->first_l2_entry, new_l2);
                    if (res <= 0) {
                        return res;
                    }
                    if (addr != 0)
                    new_l2->paddr = addr;
                    new_l2->flags = flags;
                    new_l2->status = VM_RECORD_INUSE;
                    new_l2->cache_hint = cache_hint;
                    start += new_l2pt->vaddr + apt->l1_section_size - start;
                    size -= new_l2pt->vaddr + apt->l1_section_size - start;
                    if (addr != 0)
                    addr += new_l2pt->vaddr + apt->l1_section_size - start;

                    working_l1 = (vm_abstract_pt_l1_entry_t *) new_l2pt->next;
                    vir_bytes l2pt_end = size % apt->l1_section_size;
                    res = biteoff_l1(apt, table, start, size - l2pt_end, working_l1, new_l1);
                    if (res <= 0) {
                        return res;
                    }
                    working_l1 = (vm_abstract_pt_l1_entry_t *) new_l1->next;
                    new_l1->status = VM_RECORD_INUSE;
                    new_l1->flags = flahgs;
                    new_l1->cache_hint = cache_hint;
                    start += new_l1->size;
                    if (addr != 0)
                    addr += new_l1->size;
                    size -= new_l1->size;
                    if (l2pt_end > 0) {
                        res = biteoff_l2pt_from_l1(apt, table, start, working_l1, new_l2pt);
                        if (res <= 0) {
                            return res;
                        }
                        if (addr != 0)
                        new_l2pt->paddr = addr;
                        new_l2pt->status = VM_RECORD_INUSE;
                        new_l2pt->flags = flags;
                        new_l2pt->cache_hint = cache_hint;
                        res = biteoff_l2(apt, new_l2pt, start, size, new_l2pt->first_l2_entry, new_l2);
                        new_l2->status = VM_RECORD_INUSE;
                        new_l2->flags = flags;
                        new_l2->cache_hint = cache_hint;

                        if (addr != 0)
                        new_l2->paddr = addr;
                    }
                    table->version++;
                    return 1;
                } else {
                    // Мы помещаемся в запись и старт выровнен по границе секции
                    vm_abstract_pt_l1_entry_t *new_l1;
                    vm_abstract_pt_l1_entry_t *new_l2pt;
                    vm_abstract_pt_l2_entry_t *new_l2;
                    vir_bytes l2pt_end = size % apt->l1_section_size;

                    res = biteoff_l1(apt, table, start, size - l2pt_end, working_l1, new_l1);
                    if (res <= 0) {
                        return res;
                    }
                    working_l1 = (vm_abstract_pt_l1_entry_t *)new_l1->next;
                    new_l1->status = VM_RECORD_INUSE;
                    new_l1->flags = flags;
                    new_l1->cache_hint = cache_hint;
                    if (addr != 0)
                    new_l1->paddr = addr;
                    start += new_l1->size;
                    if (addr != 0)
                    addr += new_l1->size;
                    size -= new_l1->size;
                    if (l2pt_end > 0) {
                        res = biteoff_l2pt_from_l1(apt, table, start, working_l1, new_l2pt);
                        if (res <= 0) {
                            return res;
                        }
                        if (addr != 0)
                        new_l2pt->paddr = addr;
                        new_l2pt->status = VM_RECORD_INUSE;
                        new_l2pt->flags = flags;
                        new_l2pt->cache_hint = cache_hint;
                        res = biteoff_l2(apt, new_l2pt, start, size, new_l2pt->first_l2_entry, new_l2);
                        new_l2->status = VM_RECORD_INUSE;
                        new_l2->flags = flags;
                        new_l2->cache_hint = cache_hint;
                        if (addr != 0)
                        new_l2->paddr = addr;
                    }
                    table->version++;
                    return 1;
                }
            } else {
                // Тут у нас только начало
                if (start % apt->l1_section_size > 0) {
                    // Начало в l2 таблице
                    vm_abstract_pt_l1_entry_t *new_l2pt;
                    vm_abstract_pt_l2_entry_t *new_l2;
                    res = biteoff_l2pt_from_l1(apt, table, start - (start % apt->l1_section_size), working_l1, new_l2pt);
                    if (res <= 0) {
                        return res;
                    }
                    new_l2pt->status = VM_RECORD_INUSE;
                    new_l2pt->flags = flags;
                    new_l2pt->cache_hint = cache_hint;
                    res = biteoff_l2(apt, new_l2pt, start, new_l2pt->vaddr + apt->l1_section_size - start, new_l2pt->first_l2_entry, new_l2);
                    if (res <= 0) {
                        return res;
                    }
                    new_l2->status = VM_RECORD_INUSE;
                    new_l2->flags = flags;
                    new_l2->cache_hint = cache_hint;
                    if (addr != 0)
                    new_l2->paddr = addr;
                    start += new_l2pt->vaddr + apt->l1_section_size - start;
                    if (addr != 0)
                    addr += new_l2pt->vaddr + apt->l1_section_size - start;
                    size -= new_l2pt->vaddr + apt->l1_section_size - start;
                    working_l1 = (vm_abstract_pt_l1_entry_t *) new_l2pt->next;
                }
                // Размечаем хвост, вычитаем размеченное и на следующий заход цикла
                working_l1->status = VM_RECORD_INUSE;
                working_l1->flags = flags;
                working_l1->cache_hint = cache_hint;
                if (addr != 0)
                working_l1->paddr = addr;
                start += working_l1->size;
                if (addr != 0)
                addr += working_l1->size;
                size -= working_l1->size;
            }
        }
    }

    return 0;
}

/*
 * Размапить регион карты памяти по виртуальному адресу
 * Функция проверяет размер и выделяет виртуальную память по запрошенному адресу
 * Адрес и размер должны быть выровнеными по границе l2
 */
int apt_map_region_to_addr(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, mmap_region_t *region,
                   vir_bytes start, vm_apt_flags_t flags) {
    return apt_map_phys_to_vir(apt, table, region->start, (vir_bytes) region->size, start, flags, region->cache_hint);
}


/*
 * Размапить адреса физической памяти от мксимального свободного адреса сконца
 * Функция проверяет размер и выделяет виртуальную память по максимальному адресу сконца с учётом размера региона
 * start - результат, младший адрес непрерывного пространства размером size, куда мы размапили addr
 */
int apt_map_phys_to_vir_max_free_end(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, phys_bytes addr, phys_bytes size,
                        vir_bytes *start, vm_apt_flags_t flags, mmap_cache_hint_t cache_hint) {
    vm_abstract_pt_l1_entry_t *l1_iter;
    vm_abstract_pt_l2_entry_t  *l2_iter;
    vir_bytes iter_size = (vir_bytes) size;
    vir_bytes iter_addr = 0;

    if (addr % apt->l2_page_size > 0 || size % apt->l2_page_size > 0) {
        // Незабываем, что все наши регионы памяти должны быть выровнены по границам страницы l2
        return APT_ALIGNMENT_ERROR;
    }

    int started= 0;
    //Так как мы ищем непрерывное пространство в конце, то начнём итерацию сконца
    for (res = table->last_entry; l1_iter != 0; l1_iter = (vm_abstract_pt_l1_entry_t *) l1_iter->prev) {
        if (l1_iter->status == VM_RECORD_FREE) {
            if (!started) {
                iter_addr = l1_iter->vaddr + l1_iter->size;
                started = 1;
            }
            if (iter_size <= l1_iter->size) {
              // Мы помещаемся в эту запись
              iter_addr = l1_iter->vaddr + l1_iter->size - iter_size; // Перемещаем адрес начала
              iter_size -= iter_size; // Уменьшаем необходимый размер
            } else {
              // Нам нужна ещё и следующая
                iter_addr = l1_iter->vaddr; // Перемещаем адрес начала
                iter_size -= l1_iter->size; // Уменьшаем необходимый размер
            }
        } else if (l1_iter->status == VM_RECORD_INUSE && l1_iter->type == VM_APT_L1_L2PT) {
            for (l2_iter = l1_iter->last_l2_entry; l2_iter != 0; l2_iter = (vm_abstract_pt_l2_entry_t *) l2_iter->prev) {
                if (l2_iter->status == VM_RECORD_FREE) {
                    if (!started) {
                        iter_addr = l2_iter->vaddr + l2_iter->size;
                        started = 1;
                    }
                    if (iter_size <= l2_iter->size) {
                        iter_addr = l2_iter->vaddr + l2_iter->size - iter_size;
                        iter_size -= iter_size;
                    } else {
                        iter_addr = l2_iter->vaddr;
                        iter_size -= l2_iter->size;
                    }
                } else {
                    // Наткнулись на занятую страницу, погнали искать непрерывный регион дальше
                    started = 0;
                    vir_bytes iter_size = (vir_bytes) size;
                    vir_bytes iter_addr = 0;
                }
            }
        } else {
            // Запись занята, ищем следующую
            started = 0;
            vir_bytes iter_size = (vir_bytes) size;
            vir_bytes iter_addr = 0;
        }

        if (started && iter_size == 0) {
            // Мы нашли непрерывное пространство - размечаем его и передаём адрес результата
            *start = iter_addr;
            return apt_map_phys_to_vir(apt, table, addr, (vir_bytes) size, iter_addr, flags, cache_hint);
        }
    }

    return APT_NOT_FREE;
}


/*
 * Размапить адреса физической памяти от минимально свободного адреса сначала
 * Функция проверяет размер и выделяет виртуальную память по минимальному адресу сначала с учётом размера региона
 * start - результат, младший адрес непрерывного пространства размером size, куда мы размапили addr
 */
int apt_map_phys_to_vir_min_free_start(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, phys_bytes addr, phys_bytes size,
                                     vir_bytes *start, vm_apt_flags_t flags, mmap_cache_hint_t cache_hint) {
    vm_abstract_pt_l1_entry_t *l1_iter;
    vm_abstract_pt_l2_entry_t  *l2_iter;
    vir_bytes iter_size = (vir_bytes) size;
    vir_bytes iter_addr = 0;

    if (addr % apt->l2_page_size > 0 || size % apt->l2_page_size > 0) {
        // Незабываем, что все наши регионы памяти должны быть выровнены по границам страницы l2
        return APT_ALIGNMENT_ERROR;
    }

    int started = 0;
    for (l1_iter = table->first_entry; l2_iter != 0; l1_iter = (vm_abstract_pt_l1_entry_t *) l1_iter->next) {
        if (l1_iter->status == VM_RECORD_FREE) {
            // Целиковая секция
            if (!started) {
                started = 1;
            }
            if (iter_size <= l2_iter->size) {
                iter_addr = l1_iter->vaddr + iter_size;
                iter_size -= iter_size;
            } else {
                iter_addr = l1_iter->vaddr + l2_iter->size;
                iter_size -= l1_iter->size;
            }
        } else if (l1_iter->status == VM_RECORD_INUSE && l1_iter->type == VM_APT_L1_L2PT) {
            // Секция l2pt
            for (l2_iter = l1_iter->first_l2_entry; l2_iter != 0; l2_iter = (vm_abstract_pt_l2_entry_t *) l2_iter->next) {
                if (l2_iter->status == VM_RECORD_FREE) {
                    if (!started) {
                        started = 1;
                    }
                    if (iter_size <= l2_iter->size) {
                        iter_addr = l2_iter->vaddr + iter_size;
                        iter_size -= iter_size;
                    } else {
                        iter_addr = l2_iter->vaddr + l2_iter->size;
                        iter_size -= l2_iter->size;
                    }
                } else {
                    // Наткнулись на занятую страницу, погнали искать непрерывный регион дальше
                    started = 0;
                    vir_bytes iter_size = (vir_bytes) size;
                    vir_bytes iter_addr = 0;
                }
            }
        } else {
            // Запись занята, ищем следующую
            started = 0;
            vir_bytes iter_size = (vir_bytes) size;
            vir_bytes iter_addr = 0;
        }

        if (started && iter_size == 0) {
            // Мы нашли непрерывное пространство - размечаем его и передаём адрес результата
            *start = iter_addr;
            return apt_map_phys_to_vir(apt, table, addr, (vir_bytes) size, iter_addr, flags, cache_hint);
        }
    }

    return APT_NOT_FREE;
}

/*
 * Размапить регион из карты памяти от максимально свободного адреса сконца
 * Функция проверяет размер и выделяет виртуальную память по максимальному адресу с конца с учётом размера региона
 */
int apt_map_region_to_max_free_end(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, mmap_region_t *region,
                   vir_bytes *start, vm_apt_flags_t flags) {
    return apt_map_phys_to_vir_max_free_end(apt, table, region->start, region->size, start, flags, region->cache_hint);
}

/*
 * Размапить регион из карты памяти от минимально свободного адреса снизу
 * Функция проверяет размер и выделяет виртуальную память по минимальному адресу начала с учётом размера региона
 */
int apt_map_region_to_min_free_start(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, mmap_region_t *region,
                   vir_bytes *start, vm_apt_flags_t flags) {
    return apt_map_phys_to_vir_min_free_start(apt, table, region->start, region->size, start, flags, region->cache_hint);
}

/*
 * Размапить память для страниц выделяемых по требованию
 * Страницы по требованию у нас имеют ФЛАГ VM_APF_VIRTUAL_ONLY и физический адрес 0
 */
int apt_map_on_demand(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes start, vir_bytes size,
                      vm_apt_flags_t flags, mmap_cache_hint_t cache_hint) {
    return apt_map_phys_to_vir(apt, table, 0, size, start, (flags | VM_APF_VIRTUAL_ONLY), cache_hint);
}

/*
 * Освободить виртуальную память
 * Функция на автомате производит конкатенацию свободного пространства в целиковые регионы
 * Как всегда, адреса и размеры должны быть выровнены по apt->l2_page_size
 * Но, кстати,этой функции похрену используется это пространство или нет, просто на выходе получится
 * Одно большое свободное пространство, если по краям у него тоже свободное пространство, оно сливается воедино
 * start и size могут попадать в середину записи l1 тогда мы разделим эти записи на l2pt
 */
int apt_unmap_vir_addr(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vir_bytes start, vir_bytes size) {
    int res;
    vm_abstract_pt_l1_entry_t *l1_iter;
    vm_abstract_pt_l2_entry_t *l2_iter;

    if (start % apt->l2_page_size || size % apt->l2_page_size) {
        return APT_ALIGNMENT_ERROR;
    }

    for (res = apt_find_l1_entry_by_virt_addr(table, start, l1_iter);
            l1_iter != 0;
            l1_iter = (vm_abstract_pt_l1_entry_t *) l1_iter->next) {
        if (l1_iter->type == VM_APT_L1_SECTION) {
            if (start == l1_iter->vaddr) {
                // Мы чётко в начале записи l1
                if (size == l1_iter->size) {
                    // Мы размапливаем всю эту запись
                    l1_iter->status = VM_RECORD_FREE;
                    l1_iter->paddr = 0;
                    l1_iter->flags = 0;
                    l1_iter->cache_hint = 0;
                    start += l1_iter->size;
                    size -= l1_iter->size;
                } else if (size < l1_iter->size) {
                    // конец где-то в середине
                    // Может потребоваться доразметка в l2pt
                    vm_abstract_pt_l1_entry_t *new_l1;
                    vir_bytes l2pt_end = size % apt->l1_section_size;
                    res = biteoff_l1(apt, table, start, size - l2pt_end, l1_iter, new_l1);
                    if (res <= 0) {
                        return res;
                    }
                    new_l1->status = VM_RECORD_FREE;
                    new_l1->flags = 0;
                    new_l1->cache_hint = 0;
                    new_l1->paddr = 0;
                    l1_iter = (vm_abstract_pt_l1_entry_t *) new_l1->next;
                    start += new_l1->size;
                    size -= new_l1->size;

                    if (l2pt_end) {
                        vm_abstract_pt_l2_entry_t *new_l2;
                        res = biteoff_l2pt_from_l1(apt, table, start, l1_iter, new_l1);
                        if (res <= 0) {
                            return 0;
                        }
                        res = biteoff_l2(apt, new_l1, start, l2pt_end, new_l1->first_l2_entry, new_l2);
                        if (res <= 0) {
                            return 0;
                        }
                        new_l2->status = VM_RECORD_FREE;
                        new_l2->flags = 0;
                        new_l2->cache_hint = 0;
                        new_l2->paddr;
                        l1_iter = new_l1;
                        start += new_l2->size;
                        size -= new_l2->size;
                    }
                } else {
                    // Мы вылезаем за пределы записи
                    // Освобождаем её и идём дальше
                    l1_iter->status = VM_RECORD_FREE;
                    l1_iter->paddr = 0;
                    l1_iter->flags = 0;
                    l1_iter->cache_hint = 0;
                    start += l1_iter->size;
                    size -= l1_iter->size;
                }
            } else {
                // Мы где-то в середине записи l1
                if (start + size <= l1_iter->vaddr + l1_iter->size) {
                    // Мы умещаемся в этой записи об l1
                    vm_abstract_pt_l1_entry_t *new_l1;
                    vir_bytes l2pt_start = start % apt->l1_section_size;
                    vir_bytes l2pt_end = size % apt->l1_section_size;
                    if (l2pt_start) {
                        vm_abstract_pt_l1_entry_t *new_l2pt;
                        vm_abstract_pt_l2_entry_t *new_l2;
                        res = biteoff_l2pt_from_l1(apt, table, start - l2pt_start, l1_iter, new_l2pt);
                        if (res <= 0) {
                            return res;
                        }
                        res = biteoff_l2(apt, new_l2pt, start, new_l2pt->vaddr + apt->l1_section_size - start, (vm_abstract_pt_l2_entry_t *) new_l2pt->first_l2_entry, new_l2);
                        if (res <= 0 ) {
                            return res;
                        }
                        new_l2pt->first_l2_entry->paddr = l1_iter->paddr + l1_iter->size;
                        new_l2pt->first_l2_entry->flags = l1_iter->flags;
                        new_l2pt->first_l2_entry->cache_hint = l1_iter->cache_hint;
                        new_l2->status = VM_RECORD_FREE;
                        new_l2->paddr = 0;
                        start = new_l2->vaddr + new_l2->size;
                        size -= new_l2->size;
                        l1_iter = (vm_abstract_pt_l1_entry_t *) new_l2pt->next;
                    }

                    // Выгрызаем большой кусок
                    res = biteoff_l1(apt, table, start, size - l2pt_end, l1_iter, new_l1);
                    if (res <= 0) {
                        return res;
                    }
                    new_l1->status = VM_RECORD_FREE;
                    new_l1->paddr = 0;
                    new_l1->flags = 0;
                    new_l1->cache_hint = 0;
                    l1_iter = (vm_abstract_pt_l1_entry_t *) new_l1->next;
                    start = new_l1->vaddr + new_l1->size;
                    size -= new_l1->size;

                    // Если у нас остался кусьман меньше секции l2, то мы снова делаем секцию l2pt
                    if (l2pt_end) {
                        vm_abstract_pt_l1_entry_t *new_l2pt;
                        vm_abstract_pt_l2_entry_t *new_l2;
                        res = biteoff_l2pt_from_l1(apt, table, start, l1_iter, new_l2pt);
                        if (res <= 0) {
                            return res;
                        }
                        res = biteoff_l2(apt, new_l2pt, start, l2pt_end, (vm_abstract_pt_l2_entry_t *) new_l2pt->first_l2_entry, new_l2);
                        if (res <= 0 ) {
                            return res;
                        }
                        new_l2->status = VM_RECORD_FREE;
                        new_l2->flags = 0;
                        new_l2->cache_hint = 0;
                        new_l2pt->last_l2_entry->status = ((vm_abstract_pt_l1_entry_t *)new_l2pt->next)->status;
                        new_l2pt->last_l2_entry->flags = ((vm_abstract_pt_l1_entry_t *)new_l2pt->next)->flags;
                        new_l2pt->last_l2_entry->cache_hint = ((vm_abstract_pt_l1_entry_t *)new_l2pt->next)->cache_hint;
                        start = new_l2->vaddr + new_l2->size;
                        size -= new_l2->size;
                        l1_iter = (vm_abstract_pt_l1_entry_t *) new_l2pt->next;
                    }

                } else {
                    // Мы залезаем в следующую запись
                    vir_bytes l2pt_start = start % apt->l1_section_size;
                    if (l2pt_start) {
                        vm_abstract_pt_l1_entry_t *new_l2pt;
                        vm_abstract_pt_l2_entry_t *new_l2;
                        res = biteoff_l2pt_from_l1(apt, table, start - l2pt_start, l1_iter, new_l2pt);
                        if (res <= 0) {
                            return res;
                        }
                        res = biteoff_l2(apt, new_l2pt, start, new_l2pt->vaddr + apt->l1_section_size - start, (vm_abstract_pt_l2_entry_t *) new_l2pt->first_l2_entry, new_l2);
                        if (res <= 0 ) {
                            return res;
                        }
                        new_l2pt->first_l2_entry->paddr = l1_iter->paddr + l1_iter->size;
                        new_l2pt->first_l2_entry->flags = l1_iter->flags;
                        new_l2pt->first_l2_entry->cache_hint = l1_iter->cache_hint;
                        new_l2->status = VM_RECORD_FREE;
                        new_l2->paddr = 0;
                        start = new_l2->vaddr + new_l2->size;
                        size -= new_l2->size;
                        l1_iter = (vm_abstract_pt_l1_entry_t *) new_l2pt->next;
                    }
                    l1_iter->status = VM_RECORD_FREE;
                    l1_iter->flags = 0;
                    l1_iter->cache_hint = 0;
                    l1_iter->paddr = 0;
                    start = l1_iter->vaddr + l1_iter->size;
                    size -= l1_iter->size;
                    // смело идём дальше на следующую итерацию.
                }
            }

        } else {
            // Мы попали в запись о l2pt
            // Начинаем перебирать записи l2 начиная с той, которая содержит наш адрес старта
            for (res = apt_find_l2_entry_by_virt_addr(l1_iter, start, l2_iter);
                 l2_iter != 0; l2_iter = (vm_abstract_pt_l2_entry_t *) l2_iter->next) {
                if (start == l2_iter->vaddr) {
                    if (size <= l2_iter->size) {
                        // поместились в эту запись l2
                        vm_abstract_pt_l2_entry_t *new_l2;
                        res = biteoff_l2(apt, l1_iter, start, size, l2_iter, new_l2);
                        if (res <= 0) {
                            return res;
                        }
                        new_l2->status = VM_RECORD_FREE;
                        new_l2->flags = 0;
                        new_l2->cache_hint = 0;
                        new_l2->paddr = 0;
                        l2_iter = new_l2;
                        start += new_l2->size;
                        size -= new_l2->size;
                    } else {
                        // Мы больше этой записи l2
                        l2_iter->status = VM_RECORD_FREE;
                        l2_iter->flags = 0;
                        l2_iter->cache_hint = 0;
                        l2_iter->paddr = 0;
                        start += l2_iter->size;
                        size -= l2_iter->size;
                        // Идём дальше.
                    }
                } else {
                    // Начало в середине секции l2
                    // напомню, что у нас все адреса выровнены по l2_page_size, так что мы смело можем резать эту секцию
                    vm_abstract_pt_l2_entry_t *new_l2;
                    if (l2_iter->vaddr + l2_iter->size >= start + size) {
                        // Мы уместились в это записи
                        res = biteoff_l2(apt, l1_iter, start, l2_iter->vaddr + l2_iter->size - start + size, l2_iter, new_l2);
                        if (res <= 0) {
                            return res;
                        }
                    } else {
                        // Мы больше этой записи
                        res = biteoff_l2(apt, l1_iter, start, l2_iter->vaddr + l2_iter->size - start, l2_iter, new_l2);
                        if (res <= 0) {
                            return res;
                        }
                    }
                    new_l2->status = VM_RECORD_FREE;
                    new_l2->flags = 0;
                    new_l2->cache_hint = 0;
                    start += new_l2->size;
                    size -= new_l2->size;
                    l2_iter = new_l2;
                }
                // Слепим воедино все куски
                if (((vm_abstract_pt_l2_entry_t *)l2_iter->prev)->status == VM_RECORD_FREE) {
                    vm_abstract_pt_l2_entry_t *first = (vm_abstract_pt_l2_entry_t *) l2_iter->prev;
                    concat_l2(apt, l1_iter, first, l2_iter);
                    l2_iter = first;
                }
                if (((vm_abstract_pt_l2_entry_t *)l2_iter->next)->status == VM_RECORD_FREE) {
                    vm_abstract_pt_l2_entry_t *second = (vm_abstract_pt_l2_entry_t *)l2_iter->next;
                    concat_l2(apt, l1_iter, l2_iter, second);
                }

                // Мы уместились в секции l2pt, так что спокойно выходим из цикла
                if (size == 0) {
                    break;
                }
            }
        }
        // Уменьшаем количество записей о секторах памяти
        if (l1_iter>status == VM_RECORD_FREE && ((vm_abstract_pt_l1_entry_t *) l1_iter->prev)->status == VM_RECORD_FREE) {
            vm_abstract_pt_l1_entry_t *first = (vm_abstract_pt_l1_entry_t *)l1_iter->prev;
            concat_l1(apt, table, first, l1_iter);
            l1_iter = first;
        }
        if (l1_iter>status == VM_RECORD_FREE && ((vm_abstract_pt_l1_entry_t *)l1_iter->next)->status == VM_RECORD_FREE) {
            vm_abstract_pt_l1_entry_t *second = (vm_abstract_pt_l1_entry_t *)l1_iter->next;
            concat_l1(apt, table, l1_iter, second);
        }

        // Проверим, что мы закончили освобождать всё запрошенное пространство
        if (size == 0) {
            return 1;
        }
    }

    return 1;
}

/*
 * Удалить даные о физических адресах из таблицы страниц
 * Функция предполагает, что регион размечен непрерывно
 * Является обёрткой для apt_unmap_vir_addr, просто вычисляет стартовый виртуальный адрес от физического адреса старта
 * см. описание apt_unmap_vir_addr для понимания поведения
 */
int apt_unmap_phys_addr(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, phys_bytes start, phys_bytes size) {
    int res;
    vm_abstract_pt_l1_entry_t *start_l1;
    res = apt_find_l1_entry_by_phys_addr(table, start, start_l1);
    if (res <= 0) {
        return res;
    }

    phys_bytes offset = start - start_l1->paddr;

    return apt_unmap_vir_addr(apt, table, start_l1->vaddr + (vir_bytes) offset, (vir_bytes) size);
}

/*
 * Удалить даные о регионе из таблицы страниц
 * Обёртка для apt_unmap_phys_addr(apt_unmap_vir_addr)
 * см. описание apt_unmap_phys_addr и apt_unmap_vir_addr для понимания механизма работы
 */
int apt_unmap_region(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, mmap_region_t *region) {
 return apt_unmap_phys_addr(apt, table, region->start, region->size);
}
