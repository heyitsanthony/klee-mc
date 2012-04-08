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

#include "DemotionSearcher.h"
#include "SecondChanceSearcher.h"
#include "RescanSearcher.h"
#include "TailPriority.h"
#include "BucketPriority.h"
#include "BatchingSearcher.h"
#include "BumpingMergeSearcher.h"
#include "EpochSearcher.h"
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
#include "HistoPriority.h"

using namespace llvm;
using namespace klee;

Prioritizer* UserSearcher::prFunc = NULL;
bool UsePrioritySearcher;

#define DECL_SEARCH_OPT(x,y,z)	\
	namespace { \
	cl::opt<bool> Use##x##Search("use-" y "-search", cl::init(false)); \
	cl::opt<bool> UseInterleaved##x("use-interleaved-" z, cl::init(false)); }

DECL_SEARCH_OPT(FreshBranch, "fresh-branch", "fb");
DECL_SEARCH_OPT(Random, "random", "RS");
DECL_SEARCH_OPT(Constraint, "cons", "CONS");
DECL_SEARCH_OPT(MinConstraint, "mcons", "MCONS");
DECL_SEARCH_OPT(Bucket, "bucket", "BS");
DECL_SEARCH_OPT(Tail, "tail", "TS");
DECL_SEARCH_OPT(RR, "rr", "RR");
DECL_SEARCH_OPT(Markov, "markov", "MV");
DECL_SEARCH_OPT(NonUniformRandom, "non-uniform-random", "NURS");
DECL_SEARCH_OPT(MinInst, "mininst", "MI");
DECL_SEARCH_OPT(MaxInst, "maxinst", "MXI");
DECL_SEARCH_OPT(Trough, "trough", "TR");
DECL_SEARCH_OPT(FrontierTrough, "ftrough", "FTR");
DECL_SEARCH_OPT(CondSucc, "cond", "CD");

#define SEARCH_HISTO	new RescanSearcher(new HistoPrioritizer(executor))
DECL_SEARCH_OPT(Histo, "histo", "HS");

namespace {
  cl::opt<bool>
  UseTunedSearch(
  	"use-tunedstack-search",
	cl::desc("Ignore all scheduler options. Use tuned stack."),
	cl::init(false));

  cl::opt<bool>
  UseFilterSearch(
  	"use-search-filter",
	cl::desc("Filter out unwanted functions from dispatch"),
	cl::init(false));

  cl::opt<bool> UseDemotionSearch("use-demotion-search");
  cl::opt<bool> UseBreadthFirst("use-breadth-first");
  cl::opt<bool> UsePhasedSearch("use-phased-search");
  cl::opt<bool> UseRandomPathSearch("use-random-path");

  cl::opt<bool> UseInterleavedTailRS("use-interleaved-TRS");
  cl::opt<bool> UseInterleavedDFS("use-interleaved-DFS");
  cl::opt<bool> UseInterleavedMD2UNURS("use-interleaved-MD2U-NURS");
  cl::opt<bool> UseInterleavedPerInstCountNURS("use-interleaved-icnt-NURS");
  cl::opt<bool> UseInterleavedCPInstCountNURS("use-interleaved-cpicnt-NURS");
  cl::opt<bool> UseInterleavedQueryCostNURS("use-interleaved-query-cost-NURS");
  cl::opt<bool> UseInterleavedCovNewNURS("use-interleaved-covnew-NURS");

  cl::opt<bool> UseSecondChance(
  	"use-second-chance",
	cl::desc("Give states that find new instructions extra time."),
	cl::init(false));

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
           cl::desc("Batching searches by instructions and time"));

  cl::opt<bool>
  UseEpochSearch(
  	"use-epoch-search",
	cl::desc("Treat older/run states with respect."),
	cl::init(false));


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
		return new PerInstCountWeight();
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
          UseInterleavedPerInstCountNURS ||
          UseInterleavedCPInstCountNURS ||
          UseInterleavedQueryCostNURS);
}

#define PUSH_SEARCHER_IF_SET(x, y)	if (x) s.push_back(y)
#define PUSH_ILEAV_IF_SET(x,y)	PUSH_SEARCHER_IF_SET(UseInterleaved##x, y)

#define DEFAULT_PR_SEARCHER	new RandomSearcher()
#define TAIL_RESCAN_SEARCHER	\
	new RescanSearcher(new Weight2Prioritizer<TailWeight>(1.0))
/* Research quality */
Searcher* UserSearcher::setupInterleavedSearcher(
	Executor& executor, Searcher* searcher)
{
	std::vector<Searcher *> s;

	s.push_back(searcher);
	PUSH_ILEAV_IF_SET(
		FreshBranch,
		new PrioritySearcher(
			new Weight2Prioritizer<FreshBranchWeight>(1),
			new RandomSearcher(),
			100));

	PUSH_ILEAV_IF_SET(Histo, SEARCH_HISTO);

	PUSH_ILEAV_IF_SET(
		CondSucc,
		new RescanSearcher(
			new Weight2Prioritizer<CondSuccWeight>(
				new CondSuccWeight(&executor),
				1.0)));

	PUSH_ILEAV_IF_SET(
		NonUniformRandom,
		new WeightedRandomSearcher(executor, new DepthWeight()));

	PUSH_ILEAV_IF_SET(
		MinInst,
		new RescanSearcher(
			new Weight2Prioritizer<StateInstCountWeight>(-1.0)));

	PUSH_ILEAV_IF_SET(
		MaxInst,
		new RescanSearcher(
			new Weight2Prioritizer<StateInstCountWeight>(1.0)));

	PUSH_ILEAV_IF_SET(DFS, new DFSSearcher());
	PUSH_ILEAV_IF_SET(RR, new RRSearcher());

	PUSH_ILEAV_IF_SET(Trough, new RescanSearcher(
		new Weight2Prioritizer<TroughWeight>(
			new TroughWeight(&executor), -1.0)));

	PUSH_ILEAV_IF_SET(FrontierTrough, new RescanSearcher(
		new Weight2Prioritizer<FrontierTroughWeight>(
			new FrontierTroughWeight(&executor), -1.0)));


	PUSH_ILEAV_IF_SET(
		Constraint,
		new RescanSearcher(
			new Weight2Prioritizer<ConstraintWeight>(1.0)));

	PUSH_ILEAV_IF_SET(
		MinConstraint,
		new RescanSearcher(
			new Weight2Prioritizer<ConstraintWeight>(-1.0)));


	PUSH_ILEAV_IF_SET(TailRS, TAIL_RESCAN_SEARCHER);

	PUSH_ILEAV_IF_SET(
		Markov,
		new PrioritySearcher(
			new Weight2Prioritizer<MarkovPathWeight>(100),
			DEFAULT_PR_SEARCHER));

	PUSH_ILEAV_IF_SET(
		Bucket,
		new PrioritySearcher(
			new BucketPrioritizer(), DEFAULT_PR_SEARCHER));

	PUSH_ILEAV_IF_SET(
		Tail,
		new PrioritySearcher(
			new TailPrioritizer(), DEFAULT_PR_SEARCHER));

	PUSH_ILEAV_IF_SET(
		MD2UNURS,
		new WeightedRandomSearcher(
			executor, new MinDistToUncoveredWeight()));

	PUSH_ILEAV_IF_SET(
		CovNewNURS,
		new WeightedRandomSearcher(executor, new CoveringNewWeight()));

	PUSH_ILEAV_IF_SET(
		PerInstCountNURS,
		new WeightedRandomSearcher(
			executor, new PerInstCountWeight()));

	PUSH_ILEAV_IF_SET(
		CPInstCountNURS,
		new WeightedRandomSearcher(executor, new CPInstCountWeight()));

	PUSH_ILEAV_IF_SET(
		QueryCostNURS,
		new WeightedRandomSearcher(executor, new QueryCostWeight()));

	PUSH_ILEAV_IF_SET(Random, new RandomSearcher());

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

	if (UseFreshBranchSearch) {
		searcher = new PrioritySearcher(
			new Weight2Prioritizer<FreshBranchWeight>(1),
			new RandomSearcher(),
			100);
	} else if (UseCondSuccSearch) {
		searcher = new RescanSearcher(
			new Weight2Prioritizer<CondSuccWeight>(
				new CondSuccWeight(&executor),
				1.0));
	} else if (UseHistoSearch) {
		searcher = SEARCH_HISTO;
	} else if (UseMarkovSearch) {
		searcher = new RescanSearcher(
			new Weight2Prioritizer<MarkovPathWeight>(100));
	} else if (UseMaxInstSearch) {
		searcher = new RescanSearcher(
			new Weight2Prioritizer<StateInstCountWeight>(1.0));
	} else if (UseMinInstSearch) {
		searcher = new RescanSearcher(
			new Weight2Prioritizer<StateInstCountWeight>(-1.0));
	} else if (UseConstraintSearch) {
		searcher =new RescanSearcher(
			new Weight2Prioritizer<ConstraintWeight>(1.0));
	} else if (UseTailSearch) {
		searcher = new PrioritySearcher(
			new TailPrioritizer(), DEFAULT_PR_SEARCHER);
	} else if (UseBucketSearch) {
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
	} else if (UseTroughSearch) {
		searcher = new RescanSearcher(
			new Weight2Prioritizer<TroughWeight>(
				new TroughWeight(&executor),
				-1.0));
 	} else if (UseTroughSearch) {
		searcher = new RescanSearcher(
			new Weight2Prioritizer<FrontierTroughWeight>(
				new FrontierTroughWeight(&executor),
				-1.0));
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

Searcher* UserSearcher::setupConfigSearcher(Executor& executor)
{
	Searcher	*searcher;

	searcher = setupBaseSearcher(executor);
	searcher = setupInterleavedSearcher(executor, searcher);

	/* xchk searcher should probably always be at the top */
	if (UseXChkSearcher)
		searcher = new XChkSearcher(searcher);

	if (UseDemotionSearch)
		searcher = new DemotionSearcher(searcher);

	if (UseFilterSearch)
		searcher = new FilterSearcher(executor, searcher);

	if (UseEpochSearch)
		searcher = new EpochSearcher(
			executor, new RRSearcher(), searcher);

	if (UseSecondChance)
		searcher = new SecondChanceSearcher(searcher);

	if (UseBatchingSearch)
		searcher = new BatchingSearcher(searcher);

	searcher = setupMergeSearcher(executor, searcher);

	if (UseIterativeDeepeningTimeSearch)
		searcher = new IterativeDeepeningTimeSearcher(searcher);

	if (UseStringPrune)
		searcher = new StringMerger(searcher);

	return searcher;
}

Searcher *UserSearcher::constructUserSearcher(Executor &executor)
{
	Searcher *searcher;

	if (UseTunedSearch) {
		std::vector<Searcher *>	s;
		PDFInterleavedSearcher	*p;

		/* alive states */
		s.push_back(
//searcher = 
		new PrioritySearcher(
				new Weight2Prioritizer<FreshBranchWeight>(1),
				new RandomSearcher(),
				100));
		s.push_back(
//			new RescanSearcher(
//				new Weight2Prioritizer<
//					ConstraintWeight>(-1.0)));
//			new RescanSearcher(
//				new Weight2Prioritizer<MarkovPathWeight>(100)));
			new RescanSearcher(
				new Weight2Prioritizer<
					StateInstCountWeight>(-1.0)));
//		s.push_back(
//			new RescanSearcher(
//				new Weight2Prioritizer<
//					StateInstCountWeight>(1.0)));

		searcher = new PDFInterleavedSearcher(s);
		s.clear();
		searcher = new EpochSearcher(
			executor, new RRSearcher(), searcher);

		/* filter away dying states */
		s.push_back(new FilterSearcher(
			executor, searcher, "dying_filter.txt"));

		/* dead states-- eagerly run dying */
		s.push_back(new WhitelistFilterSearcher(
			executor, new DFSSearcher(), "dying_filter.txt"));

		p = new PDFInterleavedSearcher(s);
		// 4x as many base tickets for alive side
		p->setBaseTickets(0 /* alive idx */, 4);
		searcher = p;
		searcher = new SecondChanceSearcher(searcher);
		searcher = new BatchingSearcher(searcher);
	} else
		searcher = setupConfigSearcher(executor);

	std::ostream &os = executor.getHandler().getInfoStream();

	os << "BEGIN searcher description\n";
	searcher->printName(os);
	os << "END searcher description\n";
	os.flush();

	return searcher;
}
