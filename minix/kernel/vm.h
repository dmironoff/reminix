
#ifndef _VM_H
#define _VM_H 1

/* Pseudo error codes */
#define VMSUSPEND       (-996)
#define EFAULT_SRC	(-995)
#define EFAULT_DST	(-994)

#define PHYS_COPY_CATCH(src, dst, size, a) {	\
	catch_pagefaults++;			\
	a = phys_copy(src, dst, size);		\
	catch_pagefaults--;			\
	}

//typedef struct {
//    endpoint_t      proc_ep;        /* endpoint процесса, NONE = пустой слот */
//    uint32_t        pt_handle;      /* arch-зависимый handle физ. таблицы    */
//    phys_bytes      pt_root;        /* физ. адрес корня (для быстрого load)  */
//    vm_abstract_pt_t *apt;          /* указатель на shared абстр. таблицу    */
//    uint32_t        applied_version; /* последняя применённая версия apt     */
//} kernel_proc_pt_t;




#endif


