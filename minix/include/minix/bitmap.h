#ifndef _BITMAP_H
#define _BITMAP_H

/* Self-contained: bitchunk_t and CHAR_BIT are needed by the macros below and
 * by bits_fill() (CONFIG_SMP), and users such as <minix/drivers.h> include
 * this header before <sys/types.h> and <limits.h>. */
#ifndef __ASSEMBLY__
#include <sys/types.h>		/* bitchunk_t (_NETBSD_SOURCE) */
#include <limits.h>		/* CHAR_BIT */
#endif

/* Bit map operations to manipulate bits of a simple mask variable.  The
 * shifted 1 is unsigned: 1 << 31 on an int is undefined behaviour. */
#define bit_set(mask, n)	((mask) |= (1U << (n)))
#define bit_unset(mask, n)	((mask) &= ~(1U << (n)))
#define bit_isset(mask, n)	((mask) & (1U << (n)))
#define bit_empty(mask)		((mask) = 0)
#define bit_fill(mask)		((mask) = ~0)

/* Definitions previously in kernel/const.h */
#define BITCHUNK_BITS   (sizeof(bitchunk_t) * CHAR_BIT)
#define BITMAP_CHUNKS(nr_bits) (((nr_bits)+BITCHUNK_BITS-1)/BITCHUNK_BITS)
#define MAP_CHUNK(map,bit) (map)[((bit)/BITCHUNK_BITS)]
#define CHUNK_OFFSET(bit) ((bit)%BITCHUNK_BITS)
#define CHUNK_BIT(bit) ((bitchunk_t)1 << CHUNK_OFFSET(bit))
#define GET_BIT(map,bit) ( MAP_CHUNK(map,bit) & CHUNK_BIT(bit) )
#define SET_BIT(map,bit) ( MAP_CHUNK(map,bit) |= CHUNK_BIT(bit) )
#define UNSET_BIT(map,bit) ( MAP_CHUNK(map,bit) &= ~CHUNK_BIT(bit) )

#if defined(CONFIG_SMP) && defined(__GNUC__)
#ifndef __ASSEMBLY__
static inline void bits_fill(bitchunk_t * chunks, unsigned bits)
{
	unsigned c, cnt;

	cnt = BITMAP_CHUNKS(bits);
	for (c = 0; c < cnt; c++)
		bit_fill(chunks[c]);
}
#endif
#endif


#endif	/* _BITMAP_H */
