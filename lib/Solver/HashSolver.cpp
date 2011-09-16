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

bool HashSolver::getCachedAssignment(
	const Query& q,
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values)
{
	Assignment	*a;

	cur_q = &q;
	cur_hash = qhash->hash(q);
	q_loaded = false;
	
	a = loadCachedAssignment(objects);
	if (a == NULL) return false;

	if (isMatch(a) == false) goto err;
	
	values.clear();
	foreach (it, objects.begin(), objects.end()) {
		const std::vector<unsigned char> *cur_v;
		
		cur_v = a->getBinding(*it);
		if (cur_v == NULL) goto err;
		values.push_back(*cur_v);
	}

	delete a;
	return true;
err:
	delete a;
	values.clear();
	return false;
}

void HashSolver::saveCachedAssignment(
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values)
{
	if (q_loaded == false) {
		Assignment	a(objects, values);
		a.save(getHashPath().c_str());
	}
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

bool HashSolver::computeInitialValues(
	const Query& q,
	const std::vector<const Array*> &objects,
        std::vector< std::vector<unsigned char> > &values)
{
	bool hasSolution;
	Query	neg_q = q.negateExpr();	// XXX stupid

	if (getCachedAssignment(neg_q, objects, values)) {
		hits++;
		return true;
	} else
		misses++;

	hasSolution = doComputeInitialValues(q, objects, values);
	if (hasSolution)
		saveCachedAssignment(objects, values);

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