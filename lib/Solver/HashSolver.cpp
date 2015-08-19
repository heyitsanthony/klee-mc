#include "static/Support.h"
#include "klee/Constraints.h"
#include "klee/Solver.h"
#include <iostream>
#include <fstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include "klee/util/Assignment.h"
#include <llvm/Support/CommandLine.h>

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
		cl::desc("Write solutions on eval with hcache."));

	cl::opt<bool>
	HCacheSink(
		"hcache-sink",
		cl::desc("Sink directory hits into accel cache"));

	cl::opt<bool>
	HCacheWriteSAT("hcache-write-sat", cl::desc("Write entire SAT query"));
}

bool HashSolver::isSink(void) { return HCacheSink; }
bool HashSolver::isWriteSAT(void) { return HCacheWriteSAT; }

bool HashSolver::lookupSAT(const Query& q, bool isSAT)
{
	if (isSAT && sat_hashes.count(cur_hash))
		return true;

	if (!isSAT && unsat_hashes.count(cur_hash))
		return true;

	if (qstore->lookupSAT(QHSEntry(q, cur_hash, isSAT))) {
		store_hits++;
		return true;
	}

	return false;
}

bool HashSolver::isPoisoned(const Query& q)
{
	if (poison_hashes.count(cur_hash))
		return true;

	return qstore->lookupSAT(QHSEntry(q, cur_hash, QHSEntry::ERR));
}

bool HashSolver::computeSat(const Query& q)
{
	cur_hash = qhash->hash(q);

	if (lookupSAT(q, true)) { hits++; return true; }
	if (lookupSAT(q, false)) { hits++; return false; }

	if (isPoisoned(q)) {
		hits++;
		failQuery();
		return false;
	}

	misses++;
	// std::cerr << "[HS] Hits=" << hits << ". Misses=" << misses << '\n';
	return computeSatMiss(q);
}

bool HashSolver::computeSatMiss(const Query& q)
{
	bool 		isSAT;

	isSAT = doComputeSat(q);
	if (failed()) {
		/* poisoned hash */
		qstore->saveSAT(QHSEntry(q, cur_hash, QHSEntry::ERR));
		poison_hashes.insert(cur_hash);
		return false;
	}

	qstore->saveSAT(QHSEntry(q, cur_hash, isSAT));
	if (isSAT)
		sat_hashes.insert(cur_hash);
	else
		unsat_hashes.insert(cur_hash);

	return isSAT;
}

Solver::Validity HashSolver::computeValidity(const Query& q)
{ return doComputeValiditySplit(q); }

bool HashSolver::isMatch(const Assignment& a) const
{
	if (a.satisfies(cur_q->expr) == false)
		return false;

	if (cur_q->constraints.isValid(a) == false)
		return false;

	return true;
}

ref<Expr> HashSolver::computeValueCached(const Query& q)
{
	ref<Expr>		ret;
	QHSEntry		qhs(q, qhash->hash(q));
	const ConstantExpr	*ce;

	if (qstore->lookupValue(qhs) == true) {
		/* hit in cache */
		assert (qhs.isSAT());
		hits++;
		return MK_CONST(qhs.value, q.expr->getWidth());
	}

	/* miss-- call out ot lower solver */
	misses++;
	ret = doComputeValue(q);
	if (ret.isNull() || failed())
		return ret;

	ce = dyn_cast<const ConstantExpr>(ret);
	if (ce == NULL)
		return ret;

	/* store to cache */
	qhs.qr = QHSEntry::SAT;
	qhs.value = ce->getZExtValue();
	qstore->saveValue(qhs);

	return ret;
}

ref<Expr> HashSolver::computeValue(const Query& query)
{
	/* saved entries bounded by 64-bit max */
	if (query.expr->getWidth() > 64)
		return doComputeValue(query);

	return computeValueCached(query);
}

bool HashSolver::getCachedAssignment(const Query& q, Assignment& a_out)
{
	cur_q = &q;
	cur_hash = qhash->hash(q);
	q_loaded = false;

	auto a = std::unique_ptr<Assignment>(
		loadCachedAssignment(a_out.getObjectVector()));
	if (!a || !isMatch(*a)) return false;

	a_out = *a;
	return true;
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
			qstore.reset(QHSFile::create(
				HCacheFDir.c_str(),
				HCachePendingDir.c_str()));
		} else
			qstore.reset(QHSDir::create(HCacheDir));
	} else {
		assert (HCacheFDir.size() && HCacheDir.size());
		qstore.reset(new QHSSink(
			QHSDir::create(HCacheDir),
			QHSFile::create(
				HCacheFDir.c_str(),
				HCachePendingDir.c_str())));
	}

	assert (qstore != NULL);
	assert (qhash);
	mkdir((HCacheDir + "/solution").c_str(), 0770);
}