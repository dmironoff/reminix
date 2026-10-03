/* Tests for the MINIX_YIELD trap: sched_yield(3), ipc_minix_yield() */
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <minix/ipc.h>
#include <sched.h>
#include <time.h>

#include "common.h"

#define ROUNDS		2000	/* turns per process in the ping-pong test */
#define MAX_SECS	10	/* the ping-pong test must finish within this */
#define CHILD_SECS	60	/* a player gives up after this */
#define BENCH_CALLS	200000	/* calls in the cost measurement */

/* Shared state of the ping-pong test. */
struct pingpong {
	volatile int turn;		/* whose turn it is: 0 or 1 */
	volatile int count[2];		/* turns taken by each player */
};

static double
now(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) e(0);

	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/*
 * Return values: sched_yield() succeeds; the raw trap rejects nonzero flags.
 */
static void
test95a(void)
{
	subtest = 1;

	if (sched_yield() != 0) e(1);
	if (ipc_minix_yield(0) != 0) e(2);
	if (ipc_minix_yield(1) != -EINVAL) e(3);
	if (ipc_minix_yield(0x80000000) != -EINVAL) e(4);
	if (sched_yield() != 0) e(5);
}

/*
 * One player of the ping-pong test: wait for our turn, spinning with
 * sched_yield(), then pass the turn to the other player.  Exit with 0 on
 * success.  Runs in a child process, so report errors through the exit code.
 */
static void
player(int id, int shmid)
{
	struct pingpong *pp;
	double t0;
	int i, spins;

	if ((pp = shmat(shmid, NULL, 0)) == (void *)-1)
		exit(10);

	t0 = now();

	for (i = 0; i < ROUNDS; i++) {
		for (spins = 0; pp->turn != id; spins++) {
			if (sched_yield() != 0)
				exit(11);
			if ((spins & 1023) == 1023 && now() - t0 > CHILD_SECS)
				exit(12);
		}

		pp->count[id]++;
		__sync_synchronize();
		pp->turn = !id;
	}

	if (shmdt(pp) != 0)
		exit(13);

	exit(0);
}

/*
 * Two processes of the same priority pass a turn back and forth, waiting with
 * sched_yield().  If both run on one CPU, each pass needs the waiting process
 * to give the CPU away: without a working yield every pass would cost a full
 * quantum (2 * ROUNDS * USER_QUANTUM, many minutes).  On SMP the players may
 * end up on different CPUs; the test then passes without exercising the queue
 * rotation, which is fine.
 */
static double
test95b(void)
{
	struct pingpong *pp;
	pid_t pid[2];
	double t0, elapsed;
	int id, shmid, status;

	subtest = 2;

	if ((shmid = shmget(IPC_PRIVATE, sizeof(*pp), IPC_CREAT | 0600)) < 0)
		e(1);
	if ((pp = shmat(shmid, NULL, 0)) == (void *)-1) e(2);

	pp->turn = 0;
	pp->count[0] = pp->count[1] = 0;

	t0 = now();

	for (id = 0; id < 2; id++) {
		switch (pid[id] = fork()) {
		case -1:
			e(3);
			break;
		case 0:
			player(id, shmid);
			/* NOTREACHED */
		}
	}

	for (id = 0; id < 2; id++) {
		if (pid[id] <= 0)
			continue;
		if (waitpid(pid[id], &status, 0) != pid[id]) e(4);
		if (!WIFEXITED(status)) e(5);
		else if (WEXITSTATUS(status) != 0)
			efmt("player %d exited with %d", id, WEXITSTATUS(status));
	}

	elapsed = now() - t0;

	if (pp->count[0] != ROUNDS) e(6);
	if (pp->count[1] != ROUNDS) e(7);
	if (elapsed > MAX_SECS)
		efmt("ping-pong took %.2f s, limit %d s", elapsed, MAX_SECS);

	if (shmdt(pp) != 0) e(8);
	if (shmctl(shmid, IPC_RMID, NULL) != 0) e(9);

	return elapsed;
}

/*
 * Cost of a yield with nothing to yield to (usually: the caller is alone in
 * its run queue).  A metric only, never a failure.
 */
static double
test95c(void)
{
	double t0;
	int i;

	subtest = 3;

	t0 = now();
	for (i = 0; i < BENCH_CALLS; i++)
		if (sched_yield() != 0) e(1);

	return (now() - t0) / BENCH_CALLS * 1e9;
}

int
main(int argc, char **argv)
{
	double pingpong, cost;

	start(95);

	test95a();
	pingpong = test95b();
	cost = test95c();

	printf("test95: ping-pong %d rounds %.2f s, sched_yield %.0f ns/call\n",
	    ROUNDS, pingpong, cost);

	quit();

	return 0;
}
