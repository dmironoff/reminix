/*	minix/u64.h					Author: Kees J. Bot
 *								7 Dec 1995
 * Functions to manipulate 64 bit disk addresses.
 */
#ifndef _MINIX__U64_H
#define _MINIX__U64_H

#include <sys/types.h>

static inline uint32_t ex64lo(u64_t i)
{
	return (uint32_t)i;
}

static inline uint32_t ex64hi(u64_t i)
{
	return (uint32_t)(i>>32);
}

static inline u64_t make64(uint32_t lo, uint32_t hi)
{
	return ((u64_t)hi << 32) | (u64_t)lo;
}

#endif /* _MINIX__U64_H */
