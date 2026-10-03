#include <minix/ipcconst.h>
#include <machine/asm.h>

	FLAGS = 8	/* yield flags (must be 0 for now) */

ENTRY(ipc_minix_yield)
	push	%ebp
	movl	%esp, %ebp
	push	%ebx
	movl	FLAGS(%ebp), %eax	/* eax = flags */
	movl	$0, %ebx		/* ebx = reserved, must be 0 */
	movl	$MINIX_YIELD, %ecx	/* ipc_minix_yield(flags) */
	int	$IPCVEC_INTR	/* trap to the kernel */
	pop	%ebx
	pop	%ebp
	ret
