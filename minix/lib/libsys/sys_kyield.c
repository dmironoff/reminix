//
// Created by dmironov on 18.04.2026.
//
#include "syslib.h"

/*===========================================================================*
 *                                sys_exit			     	     *
 *===========================================================================*/
int sys_kyield(void)
{
/* A system process requests to exit. */
    message m;

    return(_kernel_call(SYS_EXIT, &m));
}