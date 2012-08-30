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
#include <llvm/Support/CommandLine.h>
#include "klee/Internal/ADT/zfstream.h"

#include "HashSolver.h"
#include "QHSFile.h"

using namespace klee;
using namespace llvm;

unsigned HashSolver::hits = 0;
unsigned HashSolver::misses = 0;
unsigned HashSolver::store_hits = 0;
HashSolver::missqueue_ty HashSolver::miss_queue;

namespace
{
	/* I'm not particularly happy about all of the options,
	 * but it's necessary to make a distinction between RO and
	 * RW caches */
	cl::opt<std::string>
	HCacheDir(
		"hcache-dir",
		cl::desc("Alternative directory for hash cache."),
		cl::init("hcache"));
	cl::opt<std::string>
	HCacheFDir(
		"hcache-fdir",	/* sat.hcache; unsat.hcache */
		cl::desc("Directory with accelerated hash files."),
		cl::init(""));

	cl::opt<std::string>
	HCachePendingDir(
		"hcache-pending",
		cl::desc("Dir for hashes pending inclusion into main cache."),
		cl::init(""));

	cl::opt<bool>
	HCacheWriteSols(
		"hcache-write-sols",
		cl::desc("Write solutions on eval with hcache."),
		cl::init(false));

	cl::opt<bool>
	HCacheSink(
		"hcache-sink",
		cl::desc("Sink directory hits into accel cache"),
		cl::init(false));

	cl::opt<bool>
	HCacheWriteSAT(
		"hcache-write-sat",
		cl::desc("Write entire SAT query"),
		cl::init(false));
}

bool QHSSink::lookup(const QHSEntry& qhs)
{
	if (dst->lookup(qhs)) return true;

	if (src->lookup(qhs)) {
		dst->saveSAT(qhs);
		return true;
	}

	return false;
}

void QHSSink::saveSAT(const QHSEntry& qhs) { dst->saveSAT(qhs); }

QHSDir* QHSDir::create(void)
{
	mkdir(HCacheDir.c_str(), 0770);
	mkdir((HCacheDir + "/sat").c_str(), 0770);
	mkdir((HCacheDir + "/unsat").c_str(), 0770);
	return new QHSDir();
}

bool QHSDir::lookup(const QHSEntry& qhs)
{
	char		path[256];
	const char	*subdir;
	struct stat	s;
	bool		found;

	subdir = (qhs.isSAT) ? "sat" : "unsat";
	snprintf(
		path, 256, "%s/%s/%016lx",
		HCacheDir.c_str(), subdir, qhs.qh);

	found = stat(path, &s) == 0;
	if (found && s.st_size == 0) {
		/* seen this twice; better save the whole thing */

		/* if we're sinking into a file cache, no need to save here */
		if (!HCacheSink && HCacheWriteSAT)
			writeSAT(qhs);
	}

	return found;
}

void QHSDir::saveSAT(const QHSEntry& qhs)
{
	const char	*subdir;
	char		path[256];
	FILE		*f;

	/* dump to corresponding SAT directory */
	subdir = (qhs.isSAT) ? "sat" : "unsat";
	snprintf(
		path, 256, "%s/%s/%016lx",
		HCacheDir.c_str(), subdir, qhs.qh);
	f = fopen(path, "w");
	if (f != NULL) fclose(f);
}

void QHSDir::writeSAT(const QHSEntry& qhs)
{
	const char	*subdir;
	char		path[256];
	gzofstream	*os;

	/* dump to corresponding SAT directory */
	subdir = (qhs.isSAT) ? "sat" : "unsat";
	snprintf(path, 256, "%s/%s/%016lx", HCacheDir.c_str(), subdir, qhs.qh);

	os = new gzofstream(path, std::ios::in | std::ios::binary);
	SMTPrinter::print(*os, qhs.q, true);
	delete os;
}


bool HashSolver::lookup(const Query& q, bool isSAT)
{
	if (isSAT && sat_hashes.count(cur_hash))
		return true;

	if (!isSAT && unsat_hashes.count(cur_hash))
		return true;

	if (qstore->lookup(QHSEntry(q, cur_hash, isSAT))) {
		store_hits++;
		return true;
	}

	return false;
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
	qstore->saveSAT(QHSEntry(q, cur_hash, isSAT));
	/* XXX */
	//miss_queue.push_back(new QHSEntry(q, cur_hash, isSAT));
	//commitMisses();
}

void HashSolver::commitMisses(void)
{
	assert (0 == 1 && "XXX");
#if 0
	foreach (it, miss_queue.begin(), miss_queue.end()) {
		qstore->saveSAT(*(*it));
		delete (*it);
	}
	miss_queue.clear();
#endif
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
	if (HCacheWriteSols)
		a.save(getHashPathSolution().c_str());
}

#define DECL_HASHPATH(x,y)				\
std::string HashSolver::getHashPath##x(void) const	\
{	\
	char	path[256];	\
	snprintf(path, 256, "%s/" #y "/%016lx", HCacheDir.c_str(), cur_hash);	\
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
	if (HCacheSink == false) {
		if (HCacheFDir.size()) {
			std::cerr << "[HashSolver] Using sink file\n";
			qstore = QHSFile::create(
				HCacheFDir.c_str(),
				HCachePendingDir.c_str());
		} else
			qstore = QHSDir::create();
	} else {
		assert (HCacheFDir.size() && HCacheDir.size());
		qstore = new QHSSink(
			QHSDir::create(),
			QHSFile::create(
				HCacheFDir.c_str(),
				HCachePendingDir.c_str()));
	}

	assert (qstore != NULL);
	assert (qhash);
	mkdir((HCacheDir + "/solution").c_str(), 0770);
}

HashSolver::~HashSolver()
{
	delete qstore;
	delete qhash;
}