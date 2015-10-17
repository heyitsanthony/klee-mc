#ifndef FORKS_H
#define FORKS_H

#include "Executor.h"
#include "klee/Expr.h"
#include "static/Graph.h"
#include <functional>

namespace klee
{
class ExecutionState;
class ExprVisitor;
class KInstruction;

struct ForkInfo
{
	typedef std::vector<std::list<SeedInfo> > resseeds_ty;

	ForkInfo(const std::vector<ref<Expr>>& in_conditions)
		: resStates(in_conditions.size(), NULL)
		, res(in_conditions.size(), false)
		, conditions(in_conditions)
		, feasibleTargets(0)
		, validTargets(0)
		, forkedTargets(0)
		, forkCompact(false)
	{}
	~ForkInfo(void) {}
	void dump(std::ostream& os) const;
	unsigned size(void) const { return conditions.size(); }

	resseeds_ty& getResSeeds(void) {
		if (resSeeds) return *resSeeds;
		resSeeds = std::make_unique<resseeds_ty>(size());
		return *resSeeds;
	}

	StateVector		resStates;
	std::vector<bool>	res;
	std::vector<ref<Expr>>	conditions;
	unsigned int		feasibleTargets;
	unsigned int		validTargets;
	unsigned		forkedTargets;
	bool			isInternal;
	bool			isSeeding;
	bool			isBranch;
	bool			forkDisabled;
	unsigned		replayTargetIdx;
	bool			forkCompact;
private:
	ForkInfo(const ForkInfo& fi);
	std::unique_ptr<resseeds_ty> resSeeds;
};

class Forks
{
public:
	typedef std::function<void(const struct ForkInfo& fi)> xfer_f_t;

	Forks(Executor& _exe);
	virtual ~Forks();

	StateVector fork(
		ExecutionState &current,
		const std::vector<ref<Expr>> &conditions,
		bool isInternal,
		bool isBranch = false);
	StatePair fork(
		ExecutionState &current,
		ref<Expr> condition,
		bool isInternal);

	StatePair forkUnconditional(ExecutionState &current, bool isInternal);

	void setPreferFalseState(bool v)
	{ assert (!preferTrueState); preferFalseState = v; }
	void setPreferTrueState(bool v)
	{ assert (!preferFalseState); preferTrueState = v; }

	ExecutionState* pureFork(ExecutionState& es, bool compact=false);

	/* WARNING: may return bogus exestates / one state / nothing */
	StatePair getLastFork(void) const { return lastFork; }

	bool isQuenching(void) const { return is_quench; }
	void setQuenching(bool v) { is_quench = v; }

	void setConstraintOmit(bool v) { omit_valid_constraints = v; }
	void setForkSuppress(bool v) { suppressForks = v; }
	bool getForkSuppress(void) const { return suppressForks; }

	static bool isReplayInhibitedForks(void);

	static void setTransitions(xfer_f_t f) { transition_f = f; }
	static xfer_f_t getTransitions(void) { return transition_f; }
protected:
	virtual bool forkSetup(ExecutionState& current, struct ForkInfo& fi);
	virtual void trackBranch(ExecutionState& current, unsigned condIdx);
	virtual bool constrainFork(
		ExecutionState& es, struct ForkInfo& fi, unsigned int);
	virtual bool evalForkBranch(ExecutionState& current, struct ForkInfo& fi);
	virtual bool setupForkAffinity(
		ExecutionState& current,
		const struct ForkInfo& fi,
		unsigned* cond_idx_map);

	/* Assigns feasibility for forking condition(s) into fi.res[cond]
	* NOTE: it is the caller's responsibility to terminate the current state
	* on failure. */
	virtual bool evalForks(ExecutionState& current, struct ForkInfo& fi);

	Executor	&exe;
	bool		suppressForks;
private:
	void skipAndRandomPrune(struct ForkInfo& fi, const char* reason);
	bool addConstraint(struct ForkInfo& fi, unsigned condIndex);

	bool makeForks(ExecutionState& current, struct ForkInfo& fi);
	void constrainForks(ExecutionState& current, struct ForkInfo& fi);
	bool isRunawayBranch(KInstruction* ki);
	bool			preferTrueState;
	bool			preferFalseState;
	StatePair		lastFork;
	bool			is_quench;
	bool			omit_valid_constraints;
	static xfer_f_t		transition_f;
	static unsigned		quench_c;
	static unsigned		fork_c;
	static unsigned		fork_uniq_c;
};
}

#endif
