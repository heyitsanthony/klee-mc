#include "static/Support.h"
#include "klee/Constraints.h"
#include "klee/Solver.h"
#include "SMTPrinter.h"
#include <iostream>
#include <fstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include "klee/util/Assignment.h"
#include "llvm/Support/CommandLine.h"

#include "HashSolver.h"

using namespace klee;
using namespace llvm;

bool HashSolver::computeSat(const Query& q)
{
	bool isSat;
	isSat = doComputeSat(q);
	return isSat;
}

Solver::Validity HashSolver::computeValidity(const Query& q)
{
	Solver::Validity validity;
	validity = doComputeValidity(q);
	return validity;
}

bool HashSolver::isMatch(Assignment* a) const
{
	if (a->satisfies(cur_q->expr) == false)
		return false;

	if (!a->satisfies(cur_q->constraints.begin(), cur_q->constraints.end()))
		return false;
	return true;
}

bool HashSolver::getCachedAssignment(const Query& q, Assignment& a_out)
{
	Assignment	*a;

	cur_q = &q;
	cur_hash = qhash->hash(q);
	q_loaded = false;
	
	a = loadCachedAssignment(a_out.getObjectVector());
	if (a == NULL) return false;

	if (isMatch(a) == false) goto err;
	
	a_out = *a;
	delete a;
	return true;
err:
	delete a;
	return false;
}

void HashSolver::saveCachedAssignment(const Assignment& a)
{
	if (q_loaded) return;
	a.save(getHashPath().c_str());
}

std::string HashSolver::getHashPath(void) const
{
	char		path[128];
	snprintf(path, 128, "hcache/%08x", cur_hash);
	return std::string(path);
}

Assignment* HashSolver::loadCachedAssignment(
	const std::vector<const Array*>& objs)
{
	Assignment	*ret;

	q_loaded = false;
	ret = new Assignment(true);
	if (!ret->load(objs, getHashPath().c_str())) {
		delete ret;
		return NULL;
	}
	q_loaded = true;

	return ret;
}

bool HashSolver::computeInitialValues(const Query& q, Assignment& a)
{
	bool hasSolution;
	Query	neg_q = q.negateExpr();	// XXX stupid

	if (getCachedAssignment(neg_q, a)) {
		hits++;
		return true;
	} else
		misses++;

	hasSolution = doComputeInitialValues(q, a);
	if (hasSolution)
		saveCachedAssignment(a);

	return hasSolution;
}

HashSolver::HashSolver(Solver* s, QueryHash* in_qhash)
: SolverImplWrapper(s)
, hits(0)
, misses(0)
, qhash(in_qhash)
, cur_q(NULL)
{
	mkdir("hcache", 0770);
}

HashSolver::~HashSolver()
{
	std::cerr
		<< "HashSolver: Hits = " << hits
		<< ". Misses = " << misses << '\n';
	delete qhash;
}