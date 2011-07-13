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
: solver(s)
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

#define BEGIN_SOLVER			\
	bool	ret;			\
	if (badQuery(q)) return false;	\
	in_solver = true;

#define END_SOLVER				\
	in_solver = false;			\
	return ret;


bool PoisonCache::computeTruth(const Query& q, bool &isValid)
{
	BEGIN_SOLVER
	ret = solver->impl->computeTruth(q, isValid);
	END_SOLVER
}

bool PoisonCache::computeValidity(const Query& q, Solver::Validity &result)
{
	BEGIN_SOLVER
	ret = solver->impl->computeValidity(q, result);
	END_SOLVER
}

bool PoisonCache::computeValue(const Query& q, ref<Expr> &result)
{
	BEGIN_SOLVER
	ret = solver->impl->computeValue(q, result);
	END_SOLVER
}

bool PoisonCache::computeInitialValues(
	const Query& q,
	const std::vector<const Array*> &objects,
        std::vector< std::vector<unsigned char> > &values,
        bool &hasSolution)
{
	BEGIN_SOLVER
	ret = solver->impl->computeInitialValues(
		q, objects, values, hasSolution);
	END_SOLVER
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
