/*
 * hosttest.h -- minimal unit-test harness for ReMinix code built on the
 * host (Linux, gcc), see docs/testing.md 1д.
 *
 * Each t_*.c file is one test program: it includes this header (which also
 * provides main()) and defines its tests with TEST(name) { ... }.  Every
 * test runs in its own forked child, so a crash or a sanitizer abort fails
 * that test only.  Output is TAP: "1..N", then "ok K - name" or
 * "not ok K - name" per test, diagnostics as "# ..." lines.
 *
 *	CHECK(cond)		record a failure and go on
 *	CHECK_EQ(a, b)		same, printing both values (as uintmax_t)
 *	REQUIRE(cond)		record a failure and end the test
 *	ht_rand()		64-bit pseudo-random number; the seed is
 *	ht_rand_below(n)	printed at start, HOSTTEST_SEED=N repeats a run
 *
 * Arguments: names of the tests to run (default: all).
 */
#ifndef HOSTTEST_H
#define HOSTTEST_H

#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

typedef void (*ht_fn)(void);

#define HT_MAX_TESTS	256

static struct {
	const char *name;
	ht_fn fn;
} ht_tests[HT_MAX_TESTS];
static int ht_ntests;
static int ht_failed;		/* failed checks in the current test */
static uint64_t ht_seed;
static uint64_t ht_rng;

static void
ht_register(const char *name, ht_fn fn)
{
	if (ht_ntests == HT_MAX_TESTS) {
		fprintf(stderr, "hosttest: more than %d tests\n", HT_MAX_TESTS);
		exit(2);
	}
	ht_tests[ht_ntests].name = name;
	ht_tests[ht_ntests].fn = fn;
	ht_ntests++;
}

#define TEST(name)							\
	static void ht_test_##name(void);				\
	__attribute__((constructor)) static void			\
	ht_reg_##name(void)						\
	{								\
		ht_register(#name, ht_test_##name);			\
	}								\
	static void ht_test_##name(void)

static int
ht_check(int ok, const char *file, int line, const char *expr)
{
	if (!ok) {
		printf("# %s:%d: CHECK(%s) failed\n", file, line, expr);
		ht_failed++;
	}
	return ok;
}

static int
ht_check_eq(uintmax_t a, uintmax_t b, const char *file, int line,
	const char *ea, const char *eb)
{
	if (a != b) {
		printf("# %s:%d: CHECK_EQ(%s, %s) failed: 0x%jx != 0x%jx\n",
		    file, line, ea, eb, a, b);
		ht_failed++;
	}
	return a == b;
}

#define CHECK(cond)	ht_check(!!(cond), __FILE__, __LINE__, #cond)
#define CHECK_EQ(a, b)	ht_check_eq((uintmax_t)(a), (uintmax_t)(b),	\
			    __FILE__, __LINE__, #a, #b)
#define REQUIRE(cond)	do { if (!CHECK(cond)) exit(1); } while (0)

/* xorshift64*: fast, good enough for test sequences, never 0. */
static uint64_t
ht_rand(void)
{
	ht_rng ^= ht_rng >> 12;
	ht_rng ^= ht_rng << 25;
	ht_rng ^= ht_rng >> 27;
	return ht_rng * UINT64_C(0x2545F4914F6CDD1D);
}

static uint64_t
ht_rand_below(uint64_t n)
{
	return n ? ht_rand() % n : 0;
}

/*
 * Run fn in a forked child (seeded for test number idx).  Returns 0 if it
 * passed, 1 if a check failed (or it exited non-zero, e.g. a sanitizer
 * report), 2 if it died from a signal.  quiet: discard its output -- for
 * tests of the harness itself.
 */
static int
ht_run_isolated(ht_fn fn, int idx, int quiet)
{
	pid_t pid;
	int status;

	fflush(stdout);
	fflush(stderr);
	if ((pid = fork()) < 0) {
		perror("hosttest: fork");
		exit(2);
	}
	if (pid == 0) {
		if (quiet && (freopen("/dev/null", "w", stdout) == NULL ||
		    freopen("/dev/null", "w", stderr) == NULL))
			exit(2);
		ht_failed = 0;
		ht_rng = ht_seed ^ ((uint64_t)(idx + 1) *
		    UINT64_C(0x9E3779B97F4A7C15));
		if (ht_rng == 0)
			ht_rng = 1;
		fn();
		fflush(stdout);
		exit(ht_failed ? 1 : 0);
	}
	while (waitpid(pid, &status, 0) < 0)
		;
	if (WIFSIGNALED(status)) {
		if (!quiet)
			printf("# killed by signal %d\n", WTERMSIG(status));
		return 2;
	}
	if (WEXITSTATUS(status) != 0 && !quiet)
		printf("# exit status %d\n", WEXITSTATUS(status));
	return WEXITSTATUS(status) != 0;
}

static int
ht_selected(const char *name, int argc, char **argv)
{
	int i;

	if (argc < 2)
		return 1;
	for (i = 1; i < argc; i++)
		if (strcmp(argv[i], name) == 0)
			return 1;
	return 0;
}

int
main(int argc, char **argv)
{
	const char *s;
	int i, n, k, failed;

	if ((s = getenv("HOSTTEST_SEED")) != NULL && *s != '\0')
		ht_seed = strtoull(s, NULL, 0);
	else
		ht_seed = (uint64_t)time(NULL) ^ ((uint64_t)getpid() << 32);

	for (i = n = 0; i < ht_ntests; i++)
		n += ht_selected(ht_tests[i].name, argc, argv);
	printf("1..%d\n", n);
	printf("# seed %" PRIu64 "\n", ht_seed);

	for (i = k = failed = 0; i < ht_ntests; i++) {
		if (!ht_selected(ht_tests[i].name, argc, argv))
			continue;
		k++;
		if (ht_run_isolated(ht_tests[i].fn, i, 0) == 0) {
			printf("ok %d - %s\n", k, ht_tests[i].name);
		} else {
			printf("not ok %d - %s\n", k, ht_tests[i].name);
			failed++;
		}
	}
	return failed ? 1 : 0;
}

#endif /* HOSTTEST_H */
