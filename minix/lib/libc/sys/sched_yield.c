#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <errno.h>
#include <sched.h>

#undef sched_yield	/* <sched.h> maps it to __libc_thr_yield */

/*
 * Give the CPU to the next ready process of the same priority on this CPU
 * (MINIX_YIELD trap).  The caller keeps its priority and the rest of its
 * quantum.
 */
int
sched_yield(void)
{
	int r;

	if ((r = ipc_minix_yield(0)) != 0) {
		errno = -r;
		return -1;
	}

	return 0;
}

/* Without libpthread, thread yield is process yield. */
__weak_alias(__libc_thr_yield, sched_yield)
