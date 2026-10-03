#include "sysutil.h"

/*
 * Give the CPU to the next ready process of the same priority on this CPU.
 * The caller keeps its priority and the rest of its quantum.  Returns at once
 * if no such process is ready.  Unlike kernel calls, this is an IPC trap that
 * any process may use (see MINIX_YIELD).
 */
int
kyield(void)
{

	return ipc_minix_yield(0);
}
