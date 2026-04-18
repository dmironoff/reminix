//
// Created by dmironov on 17.04.2026.
//

#ifndef REMINIX_GLO_H
#define REMINIX_GLO_H

#include <minix/sys_config.h>
#include <minix/type.h>
#include <minix/param.h>
#include <sys/stat.h>

#include "vm.h"
#include "vmproc.h"

#if _MAIN
#undef EXTERN
#define EXTERN
#endif

#define VMP_EXECTMP	_NR_PROCS
#define VMP_NR		_NR_PROCS+1

EXTERN struct vmproc vmproc[VMP_NR];

EXTERN kinfo_t kbi;

EXTERN vm_abstract_pagetables_t *apt;
EXTERN mmap_t *mmap;
EXTERN long enable_filemap;

EXTERN int num_vm_instances;

#endif //REMINIX_GLO_H
