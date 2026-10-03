#include <minix/ipcconst.h>
#include <machine/asm.h>

ENTRY(ipc_minix_yield)
	push	{fp}
	mov	fp, sp
	mov	r1, r0		/* r1 = flags (must be 0 for now) */
	mov	r2, #0		/* r2 = reserved, must be 0 */
	mov	r0, #MINIX_YIELD /* ipc_minix_yield(flags) */
	mov	r3, #IPCVEC_INTR  /* r3 determines the SVC type */
	svc	#0           /* trap to kernel */
	pop	{fp}
	bx	lr
