#ifndef FORKS_H
#define FORKS_H

#include "Executor.h"
#include "klee/Expr.h"
#include "static/Graph.h"

namespace klee
{
class ExecutionState;
class CallPathNode;
class ExprVisitor;

class Forks
{
public:
	typedef std::set<std::pair<ref<Expr>, ref<Expr> > >	condxfer_ty;
	typedef std::set<ref<Expr> > 				succ_ty;

	struct ForkInfo
	{
		ForkInfo(
			ref<Expr>* in_conditions,
			unsigned int in_N)
		: resStates(in_N, NULL)
		, res(in_N, false)
		, conditions(in_conditions)
		, N(in_N)
		, feasibleTargets(0)
		, validTargets(0)
		, resSeeds(in_N)
		, forkCompact(false)
		{}

		Executor::StateVector	resStates;
		std::vector<bool>	res;
		ref<Expr>		*conditions;
		unsigned int		N;
		unsigned int		feasibleTargets;
		unsigned int		validTargets;
		bool			isInternal;
		bool			isSeeding;
		bool			isBranch;
		bool			wasReplayed;
		std::vector<std::list<SeedInfo> > resSeeds;
		bool			forkCompact;
	};

	Forks(Executor& _exe);
	virtual ~Forks();

	Executor::StateVector fork(
		ExecutionState &current,
		unsigned N, ref<Expr> conditions[], bool isInternal,
		bool isBranch = false);
	Executor::StatePair fork(
		ExecutionState &current,
		ref<Expr> condition,
		bool isInternal);

	Executor::StatePair forkUnconditional(
		ExecutionState &current, bool isInternal);

	void setPreferFalseState(bool v)
	{ assert (!preferTrueState); preferFalseState = v; }
	void setPreferTrueState(bool v)
	{ assert (!preferFalseState); preferTrueState = v; }

	ExecutionState* pureFork(ExecutionState& es, bool compact=false);

	condxfer_ty::const_iterator beginConds(void) const
	{ return condXfer.begin(); }
	condxfer_ty::const_iterator endConds(void) const
	{ return condXfer.end(); }

	bool hasSuccessor(ExecutionState& st) const;
	bool hasSuccessor(const ref<Expr>& cond) const;

	/* WARNING: may return bogus exestates / one state / nothing */
	Executor::StatePair getLastFork(void) const { return lastFork; }

private:
	/* this forking code really should be refactored */
	bool isForkingCondition(ExecutionState& current, ref<Expr> condition);
	bool isForkingCallPath(CallPathNode* cpn);


	void skipAndRandomPrune(struct ForkInfo& fi, const char* reason);

	bool forkSetupNoSeeding(ExecutionState& current, struct ForkInfo& fi);
	bool forkFollowReplay(ExecutionState& current, struct ForkInfo& fi);
	void forkSetupSeeding(ExecutionState& current, struct ForkInfo& fi);

	/* Assigns feasibility for forking condition(s) into fi.res[cond]
	* NOTE: it is the caller's responsibility to terminate the current state
	* on failure.
	* */
	bool evalForks(ExecutionState& current, struct ForkInfo& fi);
	bool evalForkBranch(ExecutionState& current, struct ForkInfo& fi);

	void makeForks(ExecutionState& current, struct ForkInfo& fi);
	void constrainForks(ExecutionState& current, struct ForkInfo& fi);
	void constrainFork(
		ExecutionState& current, struct ForkInfo& fi, unsigned int);

	Executor			&exe;
	bool				preferTrueState;
	bool				preferFalseState;
	//GenericGraph<ref<Expr> >	condXfer;
	condxfer_ty			condXfer;
	succ_ty				hasSucc;
	ExprVisitor			*condFilter;
	Executor::StatePair		lastFork;
};

}

#endif
