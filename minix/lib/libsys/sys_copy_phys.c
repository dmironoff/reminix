#include "syslib.h"
#include <string.h>

/* Copies from and to physical memory (SYS_COPY_PHYS_VIR, SYS_COPY_VIR_PHYS,
 * SYS_COPY_PHYS_PHYS); each kernel call has its own privilege.
 */
static int copy_phys(int call_nr, uint64_t src, uint64_t dst,
	endpoint_t endpt, size_t bytes)
{
  message m;

  if (bytes == 0) return(OK);

  memset(&m, 0, sizeof(m));
  m.m_lsys_krn_sys_copy_phys.src_addr = src;
  m.m_lsys_krn_sys_copy_phys.dst_addr = dst;
  m.m_lsys_krn_sys_copy_phys.nr_bytes = bytes;
  m.m_lsys_krn_sys_copy_phys.endpt = endpt;

  return(_kernel_call(call_nr, &m));
}

int sys_copy_phys_vir(phys_addr_t src, endpoint_t dst_e, vir_addr_t dst,
	size_t bytes)
{
/* Copy from physical memory into process dst_e (or SELF). */
  return copy_phys(SYS_COPY_PHYS_VIR, src, dst, dst_e, bytes);
}

int sys_copy_vir_phys(endpoint_t src_e, vir_addr_t src, phys_addr_t dst,
	size_t bytes)
{
/* Copy from process src_e (or SELF) into physical memory. */
  return copy_phys(SYS_COPY_VIR_PHYS, src, dst, src_e, bytes);
}

int sys_copy_phys_phys(phys_addr_t src, phys_addr_t dst, size_t bytes)
{
/* Copy from physical to physical memory. */
  return copy_phys(SYS_COPY_PHYS_PHYS, src, dst, NONE, bytes);
}
