/* The kernel call implemented in this file:
 *   m_type:	SYS_READBIOS
 *
 * The parameters for this kernel call are:
 *	m_lsys_krn_readbios.size	number of bytes to copy
 *	m_lsys_krn_readbios.addr	absolute address in BIOS area
 *	m_lsys_krn_readbios.buf		buffer address in requesting process
 */

#include "kernel/system.h"

/*===========================================================================*
 *				do_readbios				     *
 *===========================================================================*/
int do_readbios(struct proc * caller, message * m_ptr)
{
  phys_addr_t src = m_ptr->m_lsys_krn_readbios.addr;
  vir_addr_t dst = m_ptr->m_lsys_krn_readbios.buf;
  size_t len = m_ptr->m_lsys_krn_readbios.size;
  phys_addr_t limit;

  limit = src + len - 1;

#define VINRANGE(v, a, b) ((a) <= (v) && (v) <= (b))
#define SUBRANGE(a,b,c,d) (VINRANGE((a), (c), (d)) && VINRANGE((b),(c),(d)))
#define USERRANGE(a, b) SUBRANGE(src, limit, (a), (b))

  if(!USERRANGE(BIOS_MEM_BEGIN, BIOS_MEM_END) &&
     !USERRANGE(BASE_MEM_TOP, UPPER_MEM_END))
  	return EPERM;

  return copy_phys_vir(caller, src, m_ptr->m_source, dst, len);
}
