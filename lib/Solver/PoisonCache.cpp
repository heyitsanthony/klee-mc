#include "klee/Constraints.h"
#include "static/Support.h"
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <openssl/sha.h>

#include "PoisonCache.h"

using namespace klee;

static PoisonCache* g_pc = NULL;

static char altstack[SIGSTKSZ];


void PoisonCache::sig_poison(int signum, siginfo_t *si, void *p)
{
	ssize_t bw;

	/* crashed before poison cache init..? */
	if (g_pc == NULL) return;

	if (g_pc->in_solver) {
		/* save to cache */
		int	fd;
		char	path[128];
		snprintf(
			path,
			128,
			POISON_DEFAULT_PATH".%s",
			g_pc->phash->getName());
		bw = write(STDERR_FILENO, "saving ", 7);
		bw = write(STDERR_FILENO, path, strlen(path));
		bw = write(STDERR_FILENO, "\n", 1);
		fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0600);
		bw = write(fd, &g_pc->hash_last, sizeof(g_pc->hash_last));
		close(fd);
	}

	bw = write(STDERR_FILENO, "die\n", 4);
	assert (bw == 4);

	/* die. */
	kill(0, SIGKILL);

	bw = write(STDERR_FILENO, "die!\n", 5);
	assert (bw == 5);
	exit(-123);

	bw = write(STDERR_FILENO, "die!!\n", 6);
	assert (bw == 5);
}



PoisonCache::PoisonCache(Solver* s, PoisonHash* in_phash)
: SolverImplWrapper(s)
, in_solver(false)
, phash(in_phash)
{
	struct sigaction	sa;
	stack_t			stk;
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

	loadCacheFromDisk(
		(std::string(POISON_DEFAULT_PATH".")+phash->getName()).c_str());
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
	hash_last = phash->hash(q);
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

/* The naive version. WE WERE SUCH FOOLS. Blindly use the default hash
 * circa epoch. We assume it's OK. */
unsigned PHExpr::hash(const Query& q) const { return q.hash(); }


#include "klee/util/ExprVisitor.h"
class RewriteVisitor : public ExprVisitor
{
private:
	unsigned	const_counter;
public:
	RewriteVisitor()
	: ExprVisitor(true, true), const_counter(0) {}

	virtual Action visitExpr(const Expr &e)
	{
		const ConstantExpr*	ce;
		uint64_t		v;
		ref<Expr>		new_ce;
	
		ce = dyn_cast<const ConstantExpr>(&e);
		if (!ce || ce->getWidth() > 64) return Action::doChildren();

		v = ce->getZExtValue();
		if (v < 0x100000) return Action::doChildren();

		if (v != ~0ULL) v++;
		new_ce = ConstantExpr::alloc(
			(++const_counter) % v,
			ce->getWidth());
		return Action::changeTo(new_ce);
	}
};

/* I noticed that we weren't seeing much results across runs. We assume that
 * this is from nondeterministic pointers gunking up the query.
 *
 * So, rewrite expressions that look like pointers into deterministic values.
 * NOTE: We need to use several patterns here to test whether the SMT can be
 * smart about partitioning with low-value constants.
 *
 * (XXX is this true? test with experiments, follow up with literature eval)
 */
unsigned PHRewritePtr::hash(const Query& q) const
{
	ConstraintManager	cm;
	ref<Expr>		new_expr;

	foreach (it, q.constraints.begin(), q.constraints.end()) {
		RewriteVisitor	rw;
		ref<Expr>	it_e = *it;
    		ref<Expr>	e = rw.visit(it_e);

		cm.addConstraint(e);
	}

	RewriteVisitor rw;
	new_expr = rw.visit(q.expr);
	Query	new_q(cm, new_expr);

	return new_q.hash();
}

/* Just to be sure that the hash Daniel gave us didn't have a lot of
 * bad collisions, we're using a cryptographic hash on the string. */
unsigned PHExprStrSHA::hash(const Query& q) const
{
	std::string	s;
	unsigned char	md[SHA_DIGEST_LENGTH];

	s = Support::printStr(q);
	SHA1((const unsigned char*)s.c_str(), s.size(), md);

	return *reinterpret_cast<unsigned*>(md); // XXX stupid
}
