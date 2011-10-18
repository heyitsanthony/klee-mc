#include "static/Support.h"
#include "klee/Solver.h"
#include "SMTPrinter.h"
#include <iostream>
#include <fstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include "llvm/Support/CommandLine.h"

#include "PoisonCache.h"

using namespace klee;
using namespace llvm;

static PoisonCache* g_pc = NULL;

static char altstack[SIGSTKSZ];

namespace {
	cl::opt<std::string>
	PCacheDir(
		"pcache-dir",
		cl::init(""),
		cl::desc("Run STP in forked process"));
	cl::opt<bool>
	PCacheSMTLog(
		"pcache-smtlog",
		cl::desc("Print SMT test for new poisonous input."),
		cl::init(false));
}

void PoisonCache::sigpoison_save(void)
{
	ssize_t bw;
	int	fd;
	char	path[128];

	snprintf(
		path,
		128,
		"%s/"POISON_DEFAULT_PATH".%s",
		(PCacheDir.size()) ? PCacheDir.c_str() : ".",
		g_pc->phash->getName());
	bw = write(STDERR_FILENO, "saving ", 7);
	bw = write(STDERR_FILENO, path, strlen(path));
	bw = write(STDERR_FILENO, "\n", 1);
	fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0600);
	bw = write(fd, &g_pc->hash_last, sizeof(g_pc->hash_last));
	close(fd);
}


void PoisonCache::sig_poison(int signum, siginfo_t *si, void *p)
{
	ssize_t bw;

	/* crashed before poison cache init..? */
	if (g_pc == NULL) return;

	if (g_pc->in_solver) sigpoison_save();

	bw = write(STDERR_FILENO, "die!\n", 5);
	assert (bw == 5);

	/* had some problems getting this to work... */
	kill(getpid(), SIGHUP);
	kill(getpid(), SIGABRT);
	raise(SIGHUP);
	raise(SIGCHLD);
	raise(SIGTERM);
	kill(getpid(), SIGCHLD);
	kill(getpid(), SIGTERM);
	kill(getpid(), SIGKILL);

	bw = write(STDERR_FILENO, "die!\n", 5);
	assert (bw == 5);
	exit(-123);

	bw = write(STDERR_FILENO, "die!!\n", 6);
	assert (bw == 5);
	abort();

	raise(SIGTERM);
	kill(0, SIGHUP);
	kill(0, SIGTERM);
	kill(0, SIGKILL);
}



PoisonCache::PoisonCache(Solver* s, QueryHash* in_phash)
: SolverImplWrapper(s)
, in_solver(false)
, phash(in_phash)
{
	struct sigaction	sa;
	stack_t			stk;
	char			path[128];
	int			err;

	assert (g_pc == NULL && "Only one PoisonCache at a time. FIXME");
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

	snprintf(
		path,
		128,
		"%s/"POISON_DEFAULT_PATH".%s",
		(PCacheDir.size()) ? PCacheDir.c_str() : ".",
		g_pc->phash->getName());

	loadCacheFromDisk(path);
}

PoisonCache::~PoisonCache()
{
	delete phash;
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

bool PoisonCache::computeInitialValues(const Query& q, Assignment& a)
{
	BEGIN_SOLVER(bool, hasSolution, false)
	hasSolution = doComputeInitialValues(q, a);
	END_SOLVER(hasSolution)
}

bool PoisonCache::badQuery(const Query& q)
{
	hash_last = phash->hash(q);
	if (poison_hashes.count(hash_last) == 0) {
		return false;
	}

	if (PCacheSMTLog) {
		char		path[128];
		struct stat	st;
		snprintf(
			path,
			128,
			"%s/poison.%s.%x.smt",
			(PCacheDir.size()) ? PCacheDir.c_str() : ".",
			g_pc->phash->getName(),
			g_pc->hash_last);
		if (stat(path, &st) == -1) {
			std::ofstream	of(path);
			SMTPrinter::print(of, q);
		}
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
