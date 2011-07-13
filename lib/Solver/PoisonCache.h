/* 
 * Catches queries that killed us in the past so we don't try them again.
 *
 * 1. Hook SIGSEGV to catch crashes in sover
 * 2. Load cache into memory for quick access.
 * -  Check for query in cache before submitting to solver
 *    - if solver crashes, save query to cache, terminate
 *    - if solver does not crash, good times
 *
 * */
#ifndef POISON_CACHE_H
#define POISON_CACHE_H

#include "klee/Solver.h"
#include "klee/SolverImpl.h"

#include <set>
#include <signal.h>

#define POISON_DEFAULT_PATH	"poison.cache"

namespace klee
{
class PoisonCache : public SolverImpl
{
public:
	PoisonCache(Solver* s);
	virtual ~PoisonCache();

	static void sig_poison(int signum, siginfo_t*, void*);
private:
	Solver			*solver;
	bool			in_solver;
	unsigned		hash_last;
	std::set<unsigned>	poison_hashes;

	bool badQuery(const Query& q);
	void loadCacheFromDisk(const char* fname = POISON_DEFAULT_PATH);
public:

	bool computeTruth(const Query&, bool &isValid);
	bool computeValidity(const Query&, Solver::Validity &result);
	bool computeValue(const Query&, ref<Expr> &result);
	bool computeInitialValues(
		const Query&,
		const std::vector<const Array*> &objects,
		std::vector< std::vector<unsigned char> > &values,
		bool &hasSolution);

	void printName(int level = 0) const {
		klee_message("%*s" "PoisonCache containing:", 2*level, "");
		solver->printName(level + 1);
	}
};
}

#endif
