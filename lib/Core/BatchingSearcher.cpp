#include "Executor.h"
#include "klee/Internal/System/Time.h"
#include "CoreStats.h"
#include "static/Sugar.h"
#include "llvm/Support/CommandLine.h"

#include "BatchingSearcher.h"

using namespace klee;

namespace {
	llvm::cl::opt<unsigned>
	BatchInstructions(
		"batch-instructions",
		llvm::cl::desc("Number of instructions to batch"),
		llvm::cl::init(0));

	llvm::cl::opt<double>
	BatchTime(
		"batch-time",
		llvm::cl::desc("Amount of time to batch"),
		llvm::cl::init(-1.0));

	llvm::cl::opt<bool>
	BatchAdaptiveTime(
		"batch-adaptive",
		llvm::cl::desc("Amount of time to batch"),
		llvm::cl::init(false));
}

#define SETUP_BATCHING(x,y,z)		\
	baseSearcher(x)			\
	, timeBudget(y)			\
	, timeBudget_base(y)		\
	, instructionBudget(z)		\
	, lastState(0)					\
	, lastStartTime(util::estWallTime())		\
	, lastStartInstructions(stats::instructions)	\
	, lastStartCov(	\
		stats::coveredInstructions + stats::uncoveredInstructions) \
	, select_new_state(true)


BatchingSearcher::BatchingSearcher(
	Searcher *_baseSearcher,
        double _timeBudget,
        unsigned _instructionBudget)
: SETUP_BATCHING(_baseSearcher, _timeBudget, _instructionBudget)
{}

BatchingSearcher::BatchingSearcher(Searcher *_baseSearcher)
: SETUP_BATCHING(_baseSearcher, BatchTime, BatchInstructions)
{}

BatchingSearcher::~BatchingSearcher() { delete baseSearcher; }


uint64_t BatchingSearcher::getElapsedInstructions(void) const
{ return (stats::instructions - lastStartInstructions); }

double BatchingSearcher::getElapsedTime(void) const
{ return util::estWallTime() - lastStartTime; }

ExecutionState &BatchingSearcher::selectState(bool allowCompact)
{
	if (lastState != NULL && !select_new_state)
		return *lastState;

	if (baseSearcher->empty()) {
		baseSearcher->update(
			NULL, States(addedStates, States::emptySet));
		addedStates.clear();
	}

	lastState = &baseSearcher->selectState(allowCompact);
	lastStartTime = util::estWallTime();
	lastStartInstructions = stats::instructions;
	select_new_state = false;

	return *lastState;
}

template<typename C1, typename C2>
bool is_disjoint(const C1& a, const C2& b)
{
  typename C1::const_iterator i = a.begin(), ii = a.end();
  typename C2::const_iterator j = b.begin(), jj = b.end();

  while (i != ii && j != jj) {
    if (*i < *j) ++i;
    else if (*j < *i) ++j;
    else return false;
  }

  return true;
}

void BatchingSearcher::handleTimeout(void)
{
	uint64_t	total_ins;

	if (!BatchAdaptiveTime || timeBudget < 0) {
		std::cerr << "[BatchingSearch] TIMEOUT TRIGGERED\n";
		lastState = NULL;
		return;
	}

	total_ins = stats::coveredInstructions + stats::uncoveredInstructions;
	if (lastStartCov < total_ins) {
		/* found new code--
		 * maybe we can spend less time on things */
		std::cerr
			<< "[BatchingSearch] increasing time budget from "
			<< timeBudget << " to " << timeBudget*1.5 << "\n";

		timeBudget *= 1.5;
	} else {
		/* didn't find ANY new code--
		 * maybe we're not spending enough time on states */
		std::cerr
			<< "[BatchingSearch] decreasing time budget from "
			<< timeBudget << " to " << timeBudget*0.7 << "\n";
		timeBudget *= 0.7;
	}

	if (timeBudget < timeBudget_base)
		timeBudget = timeBudget_base;
	else if (timeBudget > 5.0*timeBudget_base)
		timeBudget = timeBudget_base*5.0;

	lastStartCov = total_ins;
}

void BatchingSearcher::update(ExecutionState *current, const States s)
{
	if (lastState != NULL) {
		bool exceeded_time;
		// assert(is_disjoint(s.getAdded(), s.getRemoved()));

		exceeded_time = (
			timeBudget > 0.0 &&
			getElapsedTime() > timeBudget);

		select_new_state |= exceeded_time;
		select_new_state |= (
			instructionBudget &&
			getElapsedInstructions() >= instructionBudget);

		if (exceeded_time)
			handleTimeout();
	}

	if (select_new_state == false && s.getRemoved().empty()) {
		addedStates.insert(s.getAdded().begin(), s.getAdded().end());
		return;
	}

	if (s.getRemoved().empty() == false) {
		baseSearcher->update(
			current,
			States(addedStates, States::emptySet));
		addedStates.clear();
	}

	addedStates.insert(s.getAdded().begin(), s.getAdded().end());

	if (addedStates.size() || s.getRemoved().size()) {
		baseSearcher->update(current, States(addedStates, s.getRemoved()));
		addedStates.clear();
	}

	if (select_new_state || s.getRemoved().count(lastState))
		lastState = NULL;
}

bool BatchingSearcher::empty() const
{
	if (addedStates.size())
		return false;
	return baseSearcher->empty();
}
