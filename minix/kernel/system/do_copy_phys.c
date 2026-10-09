/* The kernel calls implemented in this file:
 *   m_type:	SYS_COPY_PHYS_VIR, SYS_COPY_VIR_PHYS, SYS_COPY_PHYS_PHYS
 *
 * The parameters for these kernel calls are:
 *   m_lsys_krn_sys_copy_phys.src_addr	source address
 *   m_lsys_krn_sys_copy_phys.dst_addr	destination address
 *   m_lsys_krn_sys_copy_phys.nr_bytes	number of bytes to copy
 *   m_lsys_krn_sys_copy_phys.endpt	process of the virtual side, or SELF
 *
 * Physical memory is reached only through these calls, each with its own
 * privilege (docs/types-audit.md 9a).
 */

#include "kernel/system.h"

#if USE_COPY_PHYS

/*===========================================================================*
 *				do_copy_phys				     *
 *===========================================================================*/
int do_copy_phys(struct proc * caller, message * m_ptr)
{
  uint64_t src = m_ptr->m_lsys_krn_sys_copy_phys.src_addr;
  uint64_t dst = m_ptr->m_lsys_krn_sys_copy_phys.dst_addr;
  uint64_t bytes = m_ptr->m_lsys_krn_sys_copy_phys.nr_bytes;
  endpoint_t endpt = m_ptr->m_lsys_krn_sys_copy_phys.endpt;
  int call_nr = m_ptr->m_type;
  int proc_nr;

  /* The values must fit the types of this platform. */
  if (src != (phys_addr_t) src || dst != (phys_addr_t) dst ||
	src != (vir_addr_t) src || dst != (vir_addr_t) dst ||
	bytes != (size_t) bytes)
	return EINVAL;

  if (call_nr == SYS_COPY_PHYS_PHYS)
	return copy_phys_phys(src, dst, bytes);

  if (endpt == SELF)
	endpt = caller->p_endpoint;
  if (!isokendpt(endpt, &proc_nr))
	return EINVAL;

  if (call_nr == SYS_COPY_PHYS_VIR)
	return copy_phys_vir(caller, src, endpt, dst, bytes);
  return copy_vir_phys(caller, endpt, src, dst, bytes);
}

#endif /* USE_COPY_PHYS */
