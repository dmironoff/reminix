/* The kernel call implemented in this file:
 *   m_type:	SYS_RUNCTL
 *
 * The parameters for this kernel call are:
 *    m1_i1:	RC_ENDPT	process number to control
 *    m1_i2:	RC_ACTION	stop or resume the process
 *    m1_i3:	RC_FLAGS	request flags
 */

#include "kernel/system.h"
#include <assert.h>

#if USE_RUNCTL

/*===========================================================================*
 *				  do_runctl				     *
 *===========================================================================*/
int do_runctl(struct proc * caller, message * m_ptr)
{
/* Control a process's RTS_PROC_STOP flag. Used for process management.
 * If the process is queued sending a message or stopped for system call
 * tracing, and the RC_DELAY request flag is given, set MF_SIG_DELAY instead
 * of RTS_PROC_STOP, and send a SIGSNDELAY signal later when the process is done
 * sending (ending the delay). Used by PM for safe signal delivery.
 */
  int proc_nr, action, flags;
  register struct proc *rp;

  /* Extract the message parameters and do sanity checking. */
  if (!isokendpt(m_ptr->RC_ENDPT, &proc_nr)) return(EINVAL);
  if (iskerneln(proc_nr)) return(EPERM);
  rp = proc_addr(proc_nr);

  action = m_ptr->RC_ACTION;
  flags = m_ptr->RC_FLAGS;

  /* Is the target sending or syscall-traced? Then set MF_SIG_DELAY instead.
   * Do this only when the RC_DELAY flag is set in the request flags field.
   * The process will not become runnable before PM has called SYS_ENDKSIG.
   * Note that asynchronous messages are not covered: a process using SENDA
   * should not also install signal handlers *and* expect POSIX compliance.
   */

  if (action == RC_STOP && (flags & RC_DELAY)) {
	if (RTS_ISSET(rp, RTS_SENDING) || (rp->p_misc_flags & MF_SC_DEFER))
		rp->p_misc_flags |= MF_SIG_DELAY;

	if (rp->p_misc_flags & MF_SIG_DELAY)
		return (EBUSY);
  }

  /* Either set or clear the stop flag. */
  switch (action) {
  case RC_STOP:
#if CONFIG_SMP
	  /* check if we must stop a process on a different CPU */
	  if (rp->p_cpu != cpuid) {
		  smp_schedule_stop_proc(rp);
		  /*
		   * ReMinix: smp_schedule_stop_proc() may drop the BKL while
		   * waiting for the target's CPU, so the RC_DELAY check above
		   * can be stale. The target may have trapped into the kernel
		   * meanwhile: its CPU handles the stop request first thing
		   * after taking the BKL (context_stop -> smp_sched_handler),
		   * but then still executes the system call, and the message
		   * ends up queued (e.g. to PM itself, which is busy in this
		   * very kernel call). Returning OK here made PM receive a call
		   * from a process it believed stopped (assert in PM
		   * do_sigprocmask, test41 on 2 CPUs; docs/testing.md 4.10).
		   * Redo the check and behave as if it had been sending all
		   * along.
		   */
		  if ((flags & RC_DELAY) && (RTS_ISSET(rp, RTS_SENDING) ||
				  (rp->p_misc_flags & MF_SC_DEFER))) {
			  rp->p_misc_flags |= MF_SIG_DELAY;
			  RTS_UNSET(rp, RTS_PROC_STOP);
			  return(EBUSY);
		  }
		  break;
	  }
#endif
	  RTS_SET(rp, RTS_PROC_STOP);
	break;
  case RC_RESUME:
	assert(RTS_ISSET(rp, RTS_PROC_STOP));
	RTS_UNSET(rp, RTS_PROC_STOP);
	break;
  default:
	return(EINVAL);
  }

  return(OK);
}

#endif /* USE_RUNCTL */

