#include "syslib.h"
#include <string.h>

int sys_memset(endpoint_t who, unsigned long pattern,
	phys_bytes base, phys_bytes bytes)
{
/* Zero a block of data.  */
  message mess;

  if (bytes == 0L) return(OK);

  mess.m_lsys_krn_sys_memset.base = base;
  mess.m_lsys_krn_sys_memset.count = bytes;
  mess.m_lsys_krn_sys_memset.pattern = pattern;
  mess.m_lsys_krn_sys_memset.process = who;

  return(_kernel_call(SYS_MEMSET, &mess));
}


int sys_memset_phys(phys_addr_t base, int pattern, size_t bytes)
{
/* Fill physical memory with a byte pattern. */
  message mess;

  if (bytes == 0) return(OK);

  memset(&mess, 0, sizeof(mess));
  mess.m_lsys_krn_sys_memset_phys.base = base;
  mess.m_lsys_krn_sys_memset_phys.count = bytes;
  mess.m_lsys_krn_sys_memset_phys.pattern = pattern;

  return(_kernel_call(SYS_MEMSET_PHYS, &mess));
}
