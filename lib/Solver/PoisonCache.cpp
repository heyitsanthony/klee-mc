#include "klee/Constraints.h"
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include "PoisonCache.h"

using namespace klee;

static PoisonCache* g_pc = NULL;

static char altstack[SIGSTKSZ];

void PoisonCache::sig_poison(int signum, siginfo_t *si, void *p)
{
	ssize_t bw;

	/* crashed at a weird time..? */
	if (g_pc == NULL) return;

	if (g_pc->in_solver) {
		/* save to cache */
		int	fd;
		fd = open(
			POISON_DEFAULT_PATH,
			O_WRONLY | O_APPEND | O_CREAT,
			0600);
		bw = write(fd, &g_pc->hash_last, sizeof(g_pc->hash_last));
		close(fd);
	}

	bw = write(STDERR_FILENO, "die\n", 4);
	assert (bw == 4);

	/* die. */
	kill(0, 9);

	bw = write(STDERR_FILENO, "die!\n", 5);
	assert (bw == 5);
	exit(-123);

	bw = write(STDERR_FILENO, "die!!\n", 6);
	assert (bw == 5);
}

PoisonCache::PoisonCache(Solver* s)
: SolverImplWrapper(s)
, in_solver(false)
{
	struct sigaction	sa;
	stack_t			stk;
	int			err;

	assert (g_pc == NULL && "Only one PoisonCache at a time");
	g_pc = this;

	/* hook into sigsegv so can catch crashing STP */
	sa.sa_sigaction = PoisonCache::sig_poison;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigemptyset(&sa.sa_mask);
	err = sigaction(SIGSEGV, &sa, NULL);
	assert (err == 0 && "Wanted SIGSEGV sigaction!");
	err = sigaction(SIGABRT, &sa, NULL);

	stk.ss_sp = altstack;
	stk.ss_flags = 0;
	stk.ss_size = SIGSTKSZ;
	err = sigaltstack(&stk, NULL);

	loadCacheFromDisk();
}

PoisonCache::~PoisonCache()
{
	g_pc = NULL;
}

#define BEGIN_SOLVER(x,y,z)		\
	x	y;			\
	if (badQuery(q)) {		\
		failQuery();		\
		return z;		\
	}				\
	in_solver = true;

#define END_SOLVER(x)					\
	in_solver = false;				\
	return x;


bool PoisonCache::computeSat(const Query& q)
{
	BEGIN_SOLVER(bool, isSat, false)
	isSat = doComputeSat(q);
	END_SOLVER(isSat)
}

Solver::Validity PoisonCache::computeValidity(const Query& q)
{
	BEGIN_SOLVER(Solver::Validity, validity, Solver::Unknown)
	validity = doComputeValidity(q);
	END_SOLVER(validity)
}

ref<Expr> PoisonCache::computeValue(const Query& q)
{
	BEGIN_SOLVER(ref<Expr>, ret, NULL)
	ret = doComputeValue(q);
	END_SOLVER(ret)
}

bool PoisonCache::computeInitialValues(
	const Query& q,
	const std::vector<const Array*> &objects,
        std::vector< std::vector<unsigned char> > &values)
{
	BEGIN_SOLVER(bool, hasSolution, false)
	hasSolution = doComputeInitialValues(q, objects, values);
	END_SOLVER(hasSolution)
}

bool PoisonCache::badQuery(const Query& q)
{
	hash_last = q.hash();
	if (poison_hashes.count(hash_last) == 0) {
		return false;
	}

	fprintf(stderr, "GOOD BYE POISON QUERY\n");
	klee_warning("Skipping poisoned query.");
	return true;
}

void PoisonCache::loadCacheFromDisk(const char* fname)
{
	FILE		*f;
	unsigned	cur_hash;

	f = fopen(fname, "rb");
	if (f == NULL) return;

	while (fread(&cur_hash, sizeof(cur_hash), 1, f)) {
		poison_hashes.insert(cur_hash);
	}

	fclose(f);
}
