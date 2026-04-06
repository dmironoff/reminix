//
// Created by dmironov on 19.03.2026.
//

#include "string.h"
#include <sys/types.h>
#include <minix/endpoint.h>
#include "apt_utils.h"
#include <minix/physmemorymap.h>

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

int apt_find_undef_table(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table) {
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

int apt_find_undef_l1_entry(vm_abstract_pagetables_t *apt, vm_abstract_pt_l1_entry_t *l1_entry) {
    if (apt->l1_entries_allocated == apt->l1_entries_used) {
        return APT_ERROR_NO_MORE_UNDEF_RECORDS;
    }

    for (int i = 0; i < apt->l1_entries_allocated; i++) {
        if (apt->l1_entries[i].status == VM_RECORD_UNDEF) {
            l1_entry = &apt->l1_entries[i];
            l1_entry->status = VM_RECORD_INUSE;
            apt->l1_entries_used++;
            return 1;
        }
    }

    return APT_DEFRAGMENTATION_ERROR;
}

int apt_find_undef_l2_entry(vm_abstract_pagetables_t *apt, vm_abstract_pt_l2_entry_t *l2_entry) {
    if (apt->l2_entries_allocated == apt->l2_entries_used) {
        return APT_ERROR_NO_MORE_UNDEF_RECORDS;
    }

    for (int i = 0; i < apt->l2_entries_allocated; i++) {
        if (apt->l2_entries[i].status == VM_RECORD_UNDEF) {
            l2_entry = &apt->l2_entries[i];
            l2_entry->status = VM_RECORD_INUSE;
            apt->l2_entries_used++;
            return 1;
        }
    }

    return APT_DEFRAGMENTATION_ERROR;
}

int apt_add_table(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table) {
    vm_abstract_pt_t *new_table_entry;
    int res;

    res = apt_find_undef_table(apt, new_table_entry);
    if (res < 0) {
        return res;
    }
    new_table_entry->owner = table->owner;
    new_table_entry->prev = apt->last_pagetable;
    apt->last_pagetable->next = new_table_entry;
    apt->last_pagetable = new_table_entry;
    table = new_table_entry;
    table->version++;

    return 1;
}

int apt_unmap_l2_entry(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table,
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
    l1_entry->version++;
    table->version++;

    return 1;
}

int apt_unmap_l1_entry(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vm_abstract_pt_l1_entry_t *l1_entry) {
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
    table->version++;
    apt->l1_entries_used--;
    memset(l1_entry, 0, sizeof(vm_abstract_pt_l1_entry_t));


    return 1;
}

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

int apt_add_l1_entry(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vm_abstract_pt_l1_entry_t *l1_entry) {
    vm_abstract_pt_l1_entry_t *new_entry;
    int res;

    res = apt_find_undef_l1_entry(apt, new_entry);
    if (res < 0) {
        return res;
    }

    new_entry->version = 1;
    new_entry->size = l1_entry->size;
    new_entry->paddr = l1_entry->paddr;
    new_entry->vaddr = l1_entry->vaddr;
    new_entry->flags = l1_entry->flags;
    new_entry->cache_hint = l1_entry->cache_hint;
    new_entry->type = l1_entry->type;

    if (table->first_entry == 0) {
        table->first_entry = new_entry;
        table->last_entry = new_entry;
    } else {
        new_entry->prev = table->last_entry;
        table->last_entry->next = new_entry;
        table->last_entry = new_entry;
    }
    l1_entry = new_entry;
    table->entries_count++;
    table->entries_dirty++;
    table->version++;

    return 1;
}

int apt_add_l2_entry(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table,
                     vm_abstract_pt_l1_entry_t *l1_entry, vm_abstract_pt_l2_entry_t *l2_entry) {
    vm_abstract_pt_l2_entry_t *new_entry;
    int res;

    res = apt_find_undef_l2_entry(apt, new_entry);
    if (res < 0) {
        return res;
    }

    new_entry->version = 1;
    new_entry->cache_hint = l2_entry->cache_hint;
    new_entry->flags = l2_entry->flags;
    new_entry->paddr = l2_entry->paddr;
    new_entry->vaddr = l2_entry->vaddr;
    new_entry->size = l2_entry->size;

    if (l1_entry->first_l2_entry == 0) {
        l1_entry->first_l2_entry = new_entry;
        l1_entry->last_l2_entry = new_entry;
    } else {
        new_entry->prev = l1_entry->last_l2_entry;
        l1_entry->last_l2_entry->next = new_entry;
        l1_entry->last_l2_entry = new_entry;
    }

    l2_entry = new_entry;
    l1_entry->l2_entries_count++;
    l1_entry->version++;
    table->version++;


    return 1;
}

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