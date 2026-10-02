/*
 * <minix/bitmap.h>: bit masks (bit_*) and chunked bitmaps (*_BIT,
 * BITMAP_CHUNKS, bits_fill).  Used by the kernel (p_stale_tlb, priv
 * masks), VM, PM and drivers.
 */
#include "hosttest.h"

/* As in sys/sys/types.h; glibc has no bitchunk_t. */
typedef uint32_t bitchunk_t;

#define CONFIG_SMP 1		/* bits_fill() */
#include <minix/bitmap.h>

#define NCHUNKS		3
#define NBITS		(NCHUNKS * 32)
#define GUARD		0xa5a5a5a5u

/* Bitmap of NCHUNKS chunks between two guard chunks. */
static bitchunk_t area[NCHUNKS + 2];
#define map		(area + 1)

static void
clear_map(void)
{
	area[0] = area[NCHUNKS + 1] = GUARD;
	memset(map, 0, NCHUNKS * sizeof(bitchunk_t));
}

static void
check_guards(void)
{
	CHECK_EQ(area[0], GUARD);
	CHECK_EQ(area[NCHUNKS + 1], GUARD);
}

TEST(chunk_sizes)
{
	CHECK_EQ(BITCHUNK_BITS, 32);
	CHECK_EQ(BITMAP_CHUNKS(0), 0);
	CHECK_EQ(BITMAP_CHUNKS(1), 1);
	CHECK_EQ(BITMAP_CHUNKS(32), 1);
	CHECK_EQ(BITMAP_CHUNKS(33), 2);
	CHECK_EQ(BITMAP_CHUNKS(64), 2);
	CHECK_EQ(BITMAP_CHUNKS(65), 3);
	CHECK_EQ(CHUNK_OFFSET(31), 31);
	CHECK_EQ(CHUNK_OFFSET(32), 0);
}

/* Each bit alone: set, test, clear, and no other bit or guard touched. */
TEST(single_bits)
{
	int b, o;

	for (b = 0; b < NBITS; b++) {
		clear_map();
		SET_BIT(map, b);
		for (o = 0; o < NBITS; o++)
			if (!GET_BIT(map, o) != (o != b))
				CHECK_EQ(o, b);
		CHECK_EQ(map[b / 32], (bitchunk_t)1 << (b % 32));
		UNSET_BIT(map, b);
		CHECK(!GET_BIT(map, b));
		CHECK_EQ(map[0] | map[1] | map[2], 0);
		check_guards();
	}
}

/* UNSET_BIT clears one bit of a full map. */
TEST(unset_in_full_map)
{
	int b, i;

	for (b = 0; b < NBITS; b++) {
		clear_map();
		for (i = 0; i < NCHUNKS; i++)
			map[i] = ~(bitchunk_t)0;
		UNSET_BIT(map, b);
		CHECK(!GET_BIT(map, b));
		CHECK_EQ(map[b / 32], ~((bitchunk_t)1 << (b % 32)));
		check_guards();
	}
}

/* Random set/unset sequences against a byte-per-bit model. */
TEST(random_vs_model)
{
	unsigned char model[NBITS];
	int step, b;

	clear_map();
	memset(model, 0, sizeof(model));
	for (step = 0; step < 100000; step++) {
		b = (int)ht_rand_below(NBITS);
		if (ht_rand() & 1) {
			SET_BIT(map, b);
			model[b] = 1;
		} else {
			UNSET_BIT(map, b);
			model[b] = 0;
		}
	}
	for (b = 0; b < NBITS; b++)
		if (!GET_BIT(map, b) != !model[b])
			CHECK_EQ(b, -1);
	check_guards();
}

/* bits_fill() fills whole chunks: BITMAP_CHUNKS(bits) of them, no more. */
TEST(bits_fill)
{
	unsigned bits;
	int i;

	for (bits = 0; bits <= NBITS; bits++) {
		clear_map();
		bits_fill(map, bits);
		for (i = 0; i < NCHUNKS; i++)
			CHECK_EQ(map[i], (unsigned)i < BITMAP_CHUNKS(bits) ?
			    ~(bitchunk_t)0 : 0);
		check_guards();
	}
}

/* bit_* on a single mask variable. */
TEST(mask_bits)
{
	uint32_t mask;
	int n;

	for (n = 0; n < 32; n++) {
		bit_empty(mask);
		bit_set(mask, n);
		CHECK_EQ(mask, (uint32_t)1 << n);
		CHECK(bit_isset(mask, n));
		bit_fill(mask);
		bit_unset(mask, n);
		CHECK_EQ(mask, ~((uint32_t)1 << n));
		CHECK(!bit_isset(mask, n));
	}
}
