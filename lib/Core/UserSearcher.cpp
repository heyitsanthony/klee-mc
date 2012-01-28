//===-- UserSearcher.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "UserSearcher.h"
#include "CovSearcher.h"

#include "Searcher.h"
#include "ExecutorBC.h"

#include "klee/Common.h"

#include "llvm/Support/CommandLine.h"

#include "RescanSearcher.h"
#include "TailPriority.h"
#include "BucketPriority.h"
#include "BatchingSearcher.h"
#include "BumpingMergeSearcher.h"
#include "BFSSearcher.h"
#include "DFSSearcher.h"
#include "FilterSearcher.h"
#include "InterleavedSearcher.h"
#include "IterativeDeepeningTimeSearcher.h"
#include "PDFInterleavedSearcher.h"
#include "PhasedSearcher.h"
#include "MergingSearcher.h"
#include "RandomPathSearcher.h"
#include "RRSearcher.h"
#include "RandomSearcher.h"
#include "WeightedRandomSearcher.h"
#include "XChkSearcher.h"
#include "StringMerger.h"
#include "Weight2Prioritizer.h"
#include "PrioritySearcher.h"

using namespace llvm;
using namespace klee;

Prioritizer* UserSearcher::prFunc = NULL;
bool UsePrioritySearcher;

namespace {
  cl::opt<bool>
  UseFilterSearch(
  	"use-search-filter",
	cl::desc("Filter out unwanted functions from dispatch"),
	cl::init(false));
  cl::opt<bool>
  UseRandomSearch("use-random-search");

  cl::opt<bool>
  UseBreadthFirst("use-breadth-first");

  cl::opt<bool>
  UseInterleavedRS("use-interleaved-RS");

  cl::opt<bool>
  UseInterleavedTailRS("use-interleaved-TRS");


  cl::opt<bool>
  UseInterleavedBS("use-interleaved-BS");

  cl::opt<bool>
  UseInterleavedTS("use-interleaved-TS");

  cl::opt<bool>
  UseInterleavedDFS("use-interleaved-DFS");

  cl::opt<bool>
  UseInterleavedRR("use-interleaved-RR");

  cl::opt<bool>
  UseInterleavedMV("use-interleaved-MV");

  cl::opt<bool>
  UseInterleavedNURS("use-interleaved-NURS");

  cl::opt<bool>
  UseInterleavedMD2UNURS("use-interleaved-MD2U-NURS");

  cl::opt<bool>
  UseInterleavedInstCountNURS("use-interleaved-icnt-NURS");

  cl::opt<bool>
  UseInterleavedCPInstCountNURS("use-interleaved-cpicnt-NURS");

  cl::opt<bool>
  UseInterleavedQueryCostNURS("use-interleaved-query-cost-NURS");

  cl::opt<bool>
  UseInterleavedCovNewNURS("use-interleaved-covnew-NURS");

  cl::opt<bool>
  UseNonUniformRandomSearch("use-non-uniform-random-search");

  cl::opt<bool>
  UsePhasedSearch("use-phased-search", cl::desc("Phased Searcher."));

  cl::opt<bool>
  UseRRSearch("use-rr-search");

  cl::opt<bool>
  UseRandomPathSearch("use-random-path");

  cl::opt<std::string>
  WeightType(
    "weight-type",
    cl::desc(
    	"Set the weight type for --use-non-uniform-random-search.\n"
	"Weights: none, icnt, cpicnt, query-cost, md2u, covnew, markov"),
    cl::init("none"));
#if 0
      clEnumVal("none", "use (2^depth)"),
      clEnumVal("icnt", "use current pc exec count"),
      clEnumVal("cpicnt", "use current pc exec count"),
      clEnumVal("query-cost", "use query cost"),
      clEnumVal("md2u", "use min dist to uncovered"),
      clEnumVal("covnew", "use min dist to uncovered + coveringNew flag"),
      clEnumValEnd));
#endif

  cl::opt<bool>
  UseMerge("use-merge",
           cl::desc("Enable support for klee_merge() (experimental)"));

  cl::opt<bool>
  UseBumpMerge("use-bump-merge",
           cl::desc("Enable support for klee_merge() (extra experimental)"));

  cl::opt<bool>
  UseIterativeDeepeningTimeSearch("use-iterative-deepening-time-search",
                                    cl::desc("(experimental)"));

  cl::opt<bool>
  UseBatchingSearch("use-batching-search",
           cl::desc("Use batching searcher (run state for N instructions/time, see --batch-instructions and --batch-time"));

  cl::opt<unsigned>
  BatchInstructions(
    "batch-instructions",
    cl::desc("Number of instructions to batch with --use-batching-search"),
    cl::init(0));

  cl::opt<double>
  BatchTime("batch-time",
            cl::desc("Amount of time to batch with --use-batching-search"),
            cl::init(-1.0));

  cl::opt<bool>
  UseStringPrune("string-prune",
    cl::desc("Prune intermediate scan states on strings (busted)"),
     cl::init(false));

  cl::opt<bool>
  UseXChkSearcher(
    "xchk-searcher",
    cl::desc("On reschedule, reaffirm validate address space is same as before"),
    cl::init(false));

  cl::opt<bool>
  UseCovSearcher(
  	"use-cov-search",
	cl::desc("Greedily execute uncovered instructions"),
	cl::init(false));

  cl::opt<bool>
  UseBucketSearcher(
  	"use-bucket-search",
	cl::desc("BUCKETS"),
	cl::init(false));

  cl::opt<bool>
  UseTailSearcher(
  	"use-tail-search",
	cl::desc("TAIL"),
	cl::init(false));

  cl::opt<bool>
  UseMarkovSearcher(
  	"use-markov-search",
	cl::desc("Markov"),
	cl::init(false));


  cl::opt<bool>
  UsePDFInterleave(
  	"use-pdf-interleave",
	cl::desc("Use uncovered instruction PDF interleaver"),
	cl::init(false));

  cl::opt<bool, true>
  UsePrioritySearcherProxy(
  	"priority-search",
	cl::location(UsePrioritySearcher),
	cl::desc("Search with generic priority searcher"),
	cl::init(false));
}

static WeightFunc* getWeightFuncByName(const std::string& name)
{
	if (name == "none")
		return new DepthWeight();
	else if (name == "icnt")
		return new InstCountWeight();
	else if (name == "cpicnt")
		return new CPInstCountWeight();
	else if (name == "query-cost")
		return new QueryCostWeight();
	else if (name == "md2u")
		return new MinDistToUncoveredWeight();
	else if (name == "covnew")
		return new CoveringNewWeight();
	else if (name =="markov")
		return new MarkovPathWeight();


	assert (0 == 1 && "Unknown weight type given");
	return NULL;
}

bool UserSearcher::userSearcherRequiresMD2U() {
  return (WeightType=="md2u" || WeightType=="covnew" ||
          UseInterleavedMD2UNURS ||
          UseInterleavedCovNewNURS ||
          UseInterleavedInstCountNURS ||
          UseInterleavedCPInstCountNURS ||
          UseInterleavedQueryCostNURS);
}

#define DEFAULT_PR_SEARCHER	new RandomSearcher()

/* Research quality */
Searcher* UserSearcher::setupInterleavedSearcher(
	Executor& executor, Searcher* searcher)
{
	std::vector<Searcher *> s;

	s.push_back(searcher);

	if (UseInterleavedNURS)
		s.push_back(
			new WeightedRandomSearcher(
				executor, new DepthWeight()));

	if (UseInterleavedDFS)
		s.push_back(new DFSSearcher());

	if (UseInterleavedRR)
		s.push_back(new RRSearcher());

	if (UseInterleavedTailRS)
		s.push_back(new RescanSearcher(
			new Weight2Prioritizer<TailWeight>(1.0)));

	if (UseInterleavedMV)
		s.push_back(
			new PrioritySearcher(
				new Weight2Prioritizer<MarkovPathWeight>(100),
				DEFAULT_PR_SEARCHER));

	if (UseInterleavedBS)
		s.push_back(
			new PrioritySearcher(
				new BucketPrioritizer(), DEFAULT_PR_SEARCHER));
	if (UseInterleavedTS)
		s.push_back(
			new PrioritySearcher(
				new TailPrioritizer(), DEFAULT_PR_SEARCHER));

	if (UseInterleavedMD2UNURS)
		s.push_back(
			new WeightedRandomSearcher(
				executor, new MinDistToUncoveredWeight()));

	if (UseInterleavedCovNewNURS)
		s.push_back(
			new WeightedRandomSearcher(
				executor, new CoveringNewWeight()));

	if (UseInterleavedInstCountNURS)
		s.push_back(
			new WeightedRandomSearcher(
				executor, new InstCountWeight()));

	if (UseInterleavedCPInstCountNURS)
		s.push_back(
			new WeightedRandomSearcher(
				executor, new CPInstCountWeight()));

	if (UseInterleavedQueryCostNURS)
		s.push_back(
			new WeightedRandomSearcher(
				executor, new QueryCostWeight()));

	if (UseInterleavedRS)
		s.push_back(new RandomSearcher());

	if (s.size() != 1) {
		if (UsePDFInterleave)
			return new PDFInterleavedSearcher(s);
		else
			return new InterleavedSearcher(s);
	}

	/* No interleaved searchers defined. Don't bother with interleave obj */
	return searcher;
}

Searcher* UserSearcher::setupBaseSearcher(Executor& executor)
{
	Searcher* searcher;

	if (UseMarkovSearcher) {
		searcher = new RescanSearcher(
				new Weight2Prioritizer<MarkovPathWeight>(100));

//			DEFAULT_PR_SEARCHER);
	} else if (UseTailSearcher) {
		searcher = new PrioritySearcher(
			new TailPrioritizer(), DEFAULT_PR_SEARCHER);
	} else if (UseBucketSearcher) {
		searcher = new PrioritySearcher(
			new BucketPrioritizer(), DEFAULT_PR_SEARCHER);
	} else if (UseCovSearcher) {
		searcher = new PrioritySearcher(
			new CovPrioritizer(
				executor.getKModule(),
				*executor.getStatsTracker()),
			DEFAULT_PR_SEARCHER);
	} else if (UsePrioritySearcher) {
		assert (prFunc != NULL);
		searcher = new PrioritySearcher(
			prFunc,
//			new PrioritySearcher(
//				new BucketPrioritizer(),
//				DEFAULT_PR_SEARCHER));
			DEFAULT_PR_SEARCHER);
		prFunc = NULL;
	} else if (UsePhasedSearch) {
		searcher = new PhasedSearcher();
	} else if (UseRRSearch) {
		searcher = new RRSearcher();
	} else if (UseRandomPathSearch) {
		searcher = new RandomPathSearcher(executor);
	} else if (UseNonUniformRandomSearch) {
		searcher = new WeightedRandomSearcher(
			executor,
			getWeightFuncByName(WeightType));
	} else if (UseRandomSearch) {
		searcher = new RandomSearcher();
	} else if (UseBreadthFirst) {
		searcher = new BFSSearcher();
	} else {
		searcher = new DFSSearcher();
	}

	return searcher;
}

Searcher* UserSearcher::setupMergeSearcher(
	Executor& executor, Searcher* searcher)
{
	if (UseMerge) {
		assert(!UseBumpMerge);
		searcher = new MergingSearcher(
			dynamic_cast<ExecutorBC&>(executor), searcher);
	} else if (UseBumpMerge) {
		searcher = new BumpMergingSearcher(
		dynamic_cast<ExecutorBC&>(executor), searcher);
	}

	return searcher;
}

Searcher *UserSearcher::constructUserSearcher(Executor &executor)
{
	Searcher *searcher;

	searcher = setupBaseSearcher(executor);
	searcher = setupInterleavedSearcher(executor, searcher);

	/* xchk searcher should probably always be at the top */
	if (UseXChkSearcher)
		searcher = new XChkSearcher(searcher);

	if (UseFilterSearch)
		searcher = new FilterSearcher(executor, searcher);

	if (UseBatchingSearch)
		searcher = new BatchingSearcher(
			searcher, BatchTime, BatchInstructions);

	searcher = setupMergeSearcher(executor, searcher);

	if (UseIterativeDeepeningTimeSearch)
		searcher = new IterativeDeepeningTimeSearcher(searcher);

	if (UseStringPrune)
		searcher = new StringMerger(searcher);

	std::ostream &os = executor.getHandler().getInfoStream();

	os << "BEGIN searcher description\n";
	searcher->printName(os);
	os << "END searcher description\n";
	os.flush();

	return searcher;
}
