
#ifndef _MINIX_PARAM_H
#define _MINIX_PARAM_H 1

#include <minix/com.h>
#include <minix/const.h>


#ifdef _MINIX_SYSTEM
/* This is used to obtain system information through SYS_GETINFO. */
#define MAXMEMMAP 40
#define PARAMS_BUFFER_SIZE                  2048

typedef struct kinfo {

        /* Minix stuff */
        struct kmessages *kmessages;
        int do_serial_debug;    /* system serial output */
        int serial_debug_baud;  /* serial baud rate */
        int minix_panicing;     /* are we panicing? */
        vir_bytes               user_sp; /* where does kernel want stack set */
        vir_bytes               user_end; /* upper proc limit */
        vir_bytes               vir_kern_start; /* kernel addrspace starts */

        int nr_procs;           /* number of user processes */
        int nr_tasks;           /* number of kernel tasks */
        char release[6];        /* kernel release number */
        char version[6];        /* kernel version number */

        char                            params[PARAMS_BUFFER_SIZE];
} kinfo_t;
#endif /* _MINIX_SYSTEM */

#endif
