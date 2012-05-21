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
#include "klee/util/gzip.h"
#include "HashSolver.h"

using namespace klee;
using namespace llvm;

unsigned HashSolver::hits = 0;
unsigned HashSolver::misses = 0;
HashSolver::missqueue_ty HashSolver::miss_queue;

namespace
{
	cl::opt<std::string>
	HCacheDir(
		"hcache-dir",
		cl::desc("Alternative directory for hash cache."),
		cl::init("hcache"));
}

bool HashSolver::lookup(const Query& q, bool isSAT)
{
	char		path[256];
	const char	*subdir;
	struct stat	s;
	bool		found;

	if (isSAT && sat_hashes.count(cur_hash))
		return true;

	if (!isSAT && unsat_hashes.count(cur_hash))
		return true;

	subdir = (isSAT) ? "sat" : "unsat";
	snprintf(
		path, 256, "%s/%s/%016lx",
		HCacheDir.c_str(), subdir, cur_hash);

	found = stat(path, &s) == 0;
	if (found && s.st_size == 0) {
		/* seen this twice; better save the whole thing */
		writeSAT(q, cur_hash, isSAT);
	}

	return found;
}

bool HashSolver::computeSat(const Query& q)
{
	cur_hash = qhash->hash(q);

	if (lookup(q, true)) { hits++; return true; }
	if (lookup(q, false)) { hits++; return false; }

	misses++;
	// std::cerr << "[HS] Hits=" << hits << ". Misses=" << misses << '\n';
	return computeSatMiss(q);
}

bool HashSolver::computeSatMiss(const Query& q)
{
	bool 		isSAT;

	isSAT = doComputeSat(q);
	if (failed()) return false;

	saveSAT(q, isSAT);
	if (isSAT)
		sat_hashes.insert(cur_hash);
	else
		unsat_hashes.insert(cur_hash);

	return isSAT;
}

void HashSolver::saveSAT(const Query& q, bool isSAT)
{
	miss_queue.push_back(new MissEntry(q, cur_hash, isSAT));
	/* XXX */
	commitMisses();
}

void HashSolver::commitMisses(void)
{
	foreach (it, miss_queue.begin(), miss_queue.end()) {
		touchSAT(*(*it));
		delete (*it);
	}
	miss_queue.clear();
}

void HashSolver::writeSAT(const MissEntry& me)
{ writeSAT(me.q, me.qh, me.isSAT); }

void HashSolver::touchSAT(const MissEntry& me)
{ touchSAT(me.q, me.qh, me.isSAT); }

void HashSolver::touchSAT(
	const Query	&q,
	Expr::Hash	qh,
	bool		isSAT)
{
	const char	*subdir;
	char		path[256];
	FILE		*f;

	/* dump to corresponding SAT directory */
	subdir = (isSAT) ? "sat" : "unsat";
	snprintf(
		path, 256, "%s/%s/%016lx",
		HCacheDir.c_str(), subdir, qh);
	f = fopen(path, "w");
	if (f != NULL) fclose(f);
}

void HashSolver::writeSAT(
	const Query	&q,
	Expr::Hash	qh,
	bool		isSAT)
{
	const char	*subdir;
	char		path[256], path2[256];

	/* dump to corresponding SAT directory */
	/* XXX: how to handle too-big queries? */
	subdir = (isSAT) ? "sat" : "unsat";
	snprintf(
		path, 256, "%s/%s/%016lx.x",
		HCacheDir.c_str(), subdir, qh);
	SMTPrinter::dumpToFile(q, path);

	snprintf(path2, 256, "%s/%s/%016lx", HCacheDir.c_str(), subdir, qh);
	GZip::gzipFile(path, path2);
}

Solver::Validity HashSolver::computeValidity(const Query& q)
{ return doComputeValiditySplit(q); }

bool HashSolver::isMatch(Assignment* a) const
{
	if (a->satisfies(cur_q->expr) == false)
		return false;

	if (cur_q->constraints.isValid(*a) == false)
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
	a.save(getHashPathSolution().c_str());
}

#define DECL_HASHPATH(x,y)				\
std::string HashSolver::getHashPath##x(void) const	\
{	\
	char	path[256];	\
	snprintf(path, 256, "hcache/" #y "/%016lx", cur_hash);	\
	return std::string(path); }

DECL_HASHPATH(SAT, sat)
DECL_HASHPATH(UnSAT, unsat)
DECL_HASHPATH(Solution, solution)


Assignment* HashSolver::loadCachedAssignment(
	const std::vector<const Array*>& objs)
{
	Assignment	*ret;

	q_loaded = false;
	ret = new Assignment(true);
	if (!ret->load(objs, getHashPathSolution().c_str())) {
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
, qhash(in_qhash)
, cur_q(NULL)
{
	mkdir(HCacheDir.c_str(), 0770);
	mkdir((HCacheDir + "/sat").c_str(), 0770);
	mkdir((HCacheDir + "/unsat").c_str(), 0770);
	mkdir((HCacheDir + "/solution").c_str(), 0770);

	assert (qhash);
}

HashSolver::~HashSolver()
{
	std::cerr
		<< "HashSolver: Hits = " << hits
		<< ". Misses = " << misses << '\n';
	delete qhash;
}