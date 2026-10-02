/*
 * Tests of the harness itself: a failing check, a REQUIRE, a crash and a
 * sanitizer report must all be seen as failures, a clean test as a pass,
 * a test under TODO() as an expected failure (or a pass to report).
 */
#include "hosttest.h"

static void
passes(void)
{
	CHECK(1);
	CHECK_EQ(2 + 2, 4);
}

static void
check_fails(void)
{
	CHECK_EQ(2 + 2, 5);
	CHECK(1);
}

static void
require_fails(void)
{
	REQUIRE(0);
	abort();			/* not reached */
}

static void
todo_fails(void)
{
	TODO("known problem");
	CHECK(0);
}

static void
todo_require_fails(void)
{
	TODO("known problem");
	REQUIRE(0);
	abort();			/* not reached */
}

static void
todo_passes(void)
{
	TODO("known problem, fixed since");
	CHECK(1);
}

static void
crashes(void)
{
	raise(SIGSEGV);
}

#ifdef __SANITIZE_ADDRESS__
static void
overflows(void)
{
	volatile char *p = malloc(8);

	p[8] = 1;			/* ASan: heap-buffer-overflow */
	free((void *)p);
}
#endif

TEST(harness_pass)
{
	CHECK_EQ(ht_run_isolated(passes, 0, 1), 0);
}

TEST(harness_check_fails)
{
	CHECK_EQ(ht_run_isolated(check_fails, 0, 1), 1);
}

TEST(harness_require_fails)
{
	CHECK_EQ(ht_run_isolated(require_fails, 0, 1), 1);
}

TEST(harness_todo)
{
	CHECK_EQ(ht_run_isolated(todo_fails, 0, 1), 3);
	CHECK_EQ(ht_run_isolated(todo_require_fails, 0, 1), 3);
	CHECK_EQ(ht_run_isolated(todo_passes, 0, 1), 4);
}

TEST(harness_crash)
{
#ifdef __SANITIZE_ADDRESS__
	/* ASan catches SIGSEGV itself, reports and exits with status 1. */
	CHECK(ht_run_isolated(crashes, 0, 1) != 0);
#else
	CHECK_EQ(ht_run_isolated(crashes, 0, 1), 2);
#endif
}

TEST(harness_sanitizer)
{
#ifdef __SANITIZE_ADDRESS__
	CHECK(ht_run_isolated(overflows, 0, 1) != 0);
#endif
}

TEST(rand_repeatable)
{
	uint64_t a[4], saved = ht_rng;
	int i;

	for (i = 0; i < 4; i++)
		a[i] = ht_rand();
	ht_rng = saved;
	for (i = 0; i < 4; i++)
		CHECK_EQ(ht_rand(), a[i]);
	for (i = 0; i < 1000; i++)
		CHECK(ht_rand_below(10) < 10);
}
