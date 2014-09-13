#ifndef FORKS_H
#define FORKS_H

#include "Executor.h"
#include "klee/Expr.h"
#include "static/Graph.h"

namespace klee
{
class ExecutionState;
class ExprVisitor;
class KInstruction;

class Forks
{
public:
	typedef std::set<std::pair<ref<Expr>, ref<Expr> > >	condxfer_ty;
	typedef std::set<Expr::Hash> 				succ_ty;

	struct ForkInfo
	{
		typedef std::vector<std::list<SeedInfo> > resseeds_ty;
		ForkInfo(
			ref<Expr>* in_conditions,
			unsigned int in_N)
		: resStates(in_N, NULL)
		, res(in_N, false)
		, conditions(in_conditions)
		, N(in_N)
		, feasibleTargets(0)
		, validTargets(0)
		, forkedTargets(0)
		, forkCompact(false)
		, resSeeds(0)
		{}
		~ForkInfo(void) { if (resSeeds) delete resSeeds; }
		void dump(std::ostream& os) const;

		resseeds_ty& getResSeeds(void)
		{
			if (resSeeds == NULL) resSeeds = new resseeds_ty(N);
			return *resSeeds;
		}

		Executor::StateVector	resStates;
		std::vector<bool>	res;
		ref<Expr>		*conditions;
		unsigned int		N;
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
		resseeds_ty		*resSeeds;
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

	bool isQuenching(void) const { return is_quench; }
	void setQuenching(bool v) { is_quench = v; }

	void setConstraintOmit(bool v) { omit_valid_constraints = v; }
	void setForkSuppress(bool v) { suppressForks = v; }
	bool getForkSuppress(void) const { return suppressForks; }
protected:
	virtual bool forkSetup(ExecutionState& current, struct ForkInfo& fi);
	virtual void trackBranch(ExecutionState& current, unsigned condIdx);
	virtual bool isForkingCondition(ExecutionState& es, ref<Expr> cond);
	virtual bool constrainFork(
		ExecutionState& es, struct ForkInfo& fi, unsigned int);
	virtual bool evalForkBranch(ExecutionState& current, struct ForkInfo& fi);
	virtual bool setupForkAffinity(
		ExecutionState& current,
		struct ForkInfo& fi,
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

	void trackTransitions(const ForkInfo& fi);

	bool makeForks(ExecutionState& current, struct ForkInfo& fi);
	void constrainForks(ExecutionState& current, struct ForkInfo& fi);
	bool isRunawayBranch(KInstruction* ki);
	bool			preferTrueState;
	bool			preferFalseState;
	condxfer_ty		condXfer;
	succ_ty			hasSucc;
	ExprVisitor		*condFilter;
	Executor::StatePair	lastFork;
	bool			is_quench;
	bool			omit_valid_constraints;
	static unsigned		quench_c;
	static unsigned		fork_c;
	static unsigned		fork_uniq_c;
};
}

#endif
