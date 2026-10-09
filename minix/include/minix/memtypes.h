/*	minix/memtypes.h
 * Memory type scheme (docs/types-audit.md section 9a): physical, virtual
 * and DMA addresses and page frame numbers, with their printf formats.
 *
 * Step A1.0: the types are synonyms of the current phys_bytes, vir_bytes
 * and phys_clicks, so the machine code does not change. In step A1.6 the
 * definitions become final (vir_addr_t = uintptr_t) and the old types go.
 */
#ifndef _MINIX_MEMTYPES_H
#define _MINIX_MEMTYPES_H

#include <minix/type.h>

#ifdef CONFIG_PHYS_ADDR_64
#error "CONFIG_PHYS_ADDR_64 is not supported before step A1.8"
#endif

typedef phys_bytes phys_addr_t;		/* physical address (RAM, device) */
typedef vir_bytes vir_addr_t;		/* virtual address */
typedef phys_addr_t dma_addr_t;		/* bus address as seen by a device */
typedef phys_clicks pfn_t;		/* physical page frame number */

/* printf formats: "%" PRIxPHYS and so on. */
#define PRIxPHYS	"lx"
#define PRIxVIR		"lx"
#define PRIxDMA		PRIxPHYS

/* The only way from a physical to a DMA address; identity for now. */
static inline dma_addr_t phys_to_dma(phys_addr_t pa)
{
	return pa;
}

#endif /* _MINIX_MEMTYPES_H */
