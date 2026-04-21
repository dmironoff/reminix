//
// Created by dmironov on 17.04.2026.
//

#ifndef REMINIX_PROTO_H
#define REMINIX_PROTO_H

#include "mmap_utils.h"
#include "apt_utils.h"

int main(void);
void init_vm(void);

/*
 * Обработчики входящих IPC
 */
int do_mapcache(message *m);
int do_setmapcache(message *m);
int do_forgetcache(message *m);
int do_clearcache(message *m);
int do_remap(message *m);
int do_get_phys(message *m);
int do_get_refcount(message *m);
int do_info(message *m);
int do_rs_set_priv(message *m;
int do_rs_prepare(message *m);
int do_rs_update(message *m);
int do_rs_memctl(message *m);
int do_vfs_reply(message *m;
int do_vfs_mmap(message *m);
int do_brk(message *m);
int do_fork(message *m);
int do_exit(message *m);
int do_willexit(message *m);
int do_mmap(message *m);
int do_munmap(message *m);
int do_map_phys(message *m);

// Обработка вызова VM_PAGEFAULT от ядра
int do_pagefault(message *m);

#endif //REMINIX_PROTO_H
