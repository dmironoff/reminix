/* The kernel calls implemented in this file:
 *   m_type:	SYS_MEMSET, SYS_MEMSET_PHYS
 *
 * The parameters for this kernel call are:
 *    m_lsys_krn_sys_memset.base	(virtual address)
 *    m_lsys_krn_sys_memset.count	(returns physical address)
 *    m_lsys_krn_sys_memset.pattern	(pattern byte to be written)
 *
 *    m_lsys_krn_sys_memset_phys.base	(physical address)
 *    m_lsys_krn_sys_memset_phys.count	(number of bytes)
 *    m_lsys_krn_sys_memset_phys.pattern	(pattern byte to be written)
 */

#include "kernel/system.h"

#if USE_MEMSET

/*===========================================================================*
 *				do_memset				     *
 *===========================================================================*/
int do_memset(struct proc * caller, message * m_ptr)
{
/* Handle sys_memset(). This writes a pattern into the specified memory. */
  return vm_memset(caller, m_ptr->m_lsys_krn_sys_memset.process,
	  m_ptr->m_lsys_krn_sys_memset.base,
	  m_ptr->m_lsys_krn_sys_memset.pattern,
	  m_ptr->m_lsys_krn_sys_memset.count);
}

#endif /* USE_MEMSET */

#if USE_MEMSET_PHYS

/*===========================================================================*
 *				do_memset_phys				     *
 *===========================================================================*/
int do_memset_phys(struct proc * caller, message * m_ptr)
{
/* Handle sys_memset_phys(): write a pattern into physical memory. */
  uint64_t base = m_ptr->m_lsys_krn_sys_memset_phys.base;
  uint64_t count = m_ptr->m_lsys_krn_sys_memset_phys.count;

  if (base != (phys_addr_t) base || count != (size_t) count)
	return EINVAL;

  return memset_phys(caller, base, m_ptr->m_lsys_krn_sys_memset_phys.pattern,
	count);
}

#endif /* USE_MEMSET_PHYS */
