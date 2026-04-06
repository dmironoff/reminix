//
// Created by dmironov on 20.03.2026.
//

#ifndef REMINIX_APT_UTILS_H
#define REMINIX_APT_UTILS_H

#include <minix/abstract_pagetables.h>

#define APT_ERROR_NO_MORE_UNDEF_RECORDS       -1
#define APT_NOT_FOUND                         -2
#define APT_DEFRAGMENTATION_ERROR             -3

int apt_find_table_by_endpoint(vm_abstract_pagetables_t *apt, endpoint_t endpoint, vm_abstract_pt_t *table);
int apt_find_table_by_phys_addr(vm_abstract_pagetables_t *apt, phys_bytes addr, vm_abstract_pt_t *table);
int apt_find_undef_table(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table);
int apt_find_undef_l1_entry(vm_abstract_pagetables_t *apt, vm_abstract_pt_l1_entry_t *l1_entry);
int apt_find_undef_l2_entry(vm_abstract_pagetables_t *apt, vm_abstract_pt_l2_entry_t *l2_entry);
int apt_add_table(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table);
int apt_unmap_l2_entry(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table,
                       vm_abstract_pt_l1_entry_t *l1_entry, vm_abstract_pt_l2_entry_t *l2_entry);
int apt_unmap_l1_entry(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vm_abstract_pt_l1_entry_t *l1_entry);
int apt_unmap_table(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table);
int apt_add_l1_entry(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table, vm_abstract_pt_l1_entry_t *l1_entry);
int apt_add_l2_entry(vm_abstract_pagetables_t *apt, vm_abstract_pt_t *table,
                     vm_abstract_pt_l1_entry_t *l1_entry, vm_abstract_pt_l2_entry_t *l2_entry);
int apt_find_l1_entry_by_virt_addr(vm_abstract_pt_t *table, vir_bytes addr, vm_abstract_pt_l1_entry_t *l1_entry);
int apt_find_l1_entry_by_phys_addr(vm_abstract_pt_t *table, phys_bytes addr, vm_abstract_pt_l1_entry_t *l1_entry);
int apt_find_l2_entry_by_virt_addr(vm_abstract_pt_l1_entry_t *l1_entry, vir_bytes addr, vm_abstract_pt_l2_entry_t *l2_entry);
int apt_find_l2_entry_by_phys_addr(vm_abstract_pt_l1_entry_t *l1_entry, phys_bytes addr, vm_abstract_pt_l2_entry_t *l2_entry);

#endif //REMINIX_APT_UTILS_H
