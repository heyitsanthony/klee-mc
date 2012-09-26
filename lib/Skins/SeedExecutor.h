#ifndef SEEDEXECUTOR_H
#define SEEDEXECUTOR_H

#include "klee/Common.h"
#include "../Core/ExeStateManager.h"
#include "../Core/ObjectState.h"
#include "../Core/ForksSeeding.h"
#include "SeedCore.h"

namespace klee
{

class ExecutionState;
class InterpreterHandler;

template<typename T>
class SeedExecutor : public T
{
public:
	SeedExecutor(InterpreterHandler* ie)
	: T(ie), seedCore(this) {}
	virtual ~SeedExecutor() {}

	virtual void executeGetValue(
		ExecutionState &state, ref<Expr> e, KInstruction *target)
	{
		if (seedCore.executeGetValueSeeding(state, e, target))
			return;
		T::executeGetValue(state, e, target);
	}

	virtual void terminate(ExecutionState &state)
	{
		// never reached searcher, just delete immediately
		seedCore.erase(&state);
		T::terminate(state);
	}

	void terminateOnError(
		ExecutionState &state,
		const llvm::Twine &messaget,
		const char *suffix,
		const llvm::Twine &info,
		bool alwaysEmit)
	{ T::terminateOnError(state, messaget, suffix, info, true); }

	void useSeeds(const std::vector<struct KTest *> *seeds)
	{ seedCore.useSeeds(seeds); }

	virtual void runLoop(void)
	{
		if (seedCore.isUsingSeeds()) {
			ExecutionState	*st(T::currentState);
			bool		ok;
			Forks		*old_f;

			old_f = T::forking;
			T::forking = new ForksSeeding(*this);
			ok = seedCore.seedRun(*st);
			delete T::forking;
			T::forking = old_f;

			if (!ok) return;

			klee_message(
				"seeding done (%d states remain)", 
				(int) T::stateManager->size());

			if (seedCore.isOnlySeed()) return;

			// XXX total hack,
			// just because I like non uniform better but want
			// seed results to be equally weighted.
			T::stateManager->setWeights(1.0);
		}

		T::runLoop();
	}

	virtual ObjectState* makeSymbolic(
		ExecutionState& state,
		const MemoryObject* mo,
		ref<Expr> len,
		const char* arrPrefix = "arr")
	{
		ObjectState	*os;
		os = T::makeSymbolic(state, mo, len, arrPrefix);
		seedCore.addSymbolicToSeeds(state, mo, os->getArray());
		return os;
	}

	virtual void replaceStateImmForked(
		ExecutionState* os, ExecutionState* ns)
	{
		seedCore.erase(os);
		T::replaceStateImmForked(os, ns);
	}

	virtual bool addConstraint(ExecutionState &state, ref<Expr> condition)
	{
		seedCore.checkAddConstraintSeeds(state, condition);
		return T::addConstraint(state, condition);
	}

	virtual bool isInterestingTestCase(ExecutionState* st) const
	{ return	T::isInterestingTestCase(st) ||
			seedCore.isInterestingTestCase(st); }

	virtual bool isStateSeeding(ExecutionState* s) const
	{ return seedCore.isStateSeeding(s); }

	virtual	SeedMapType& getSeedMap(void) { return seedCore.getSeedMap(); }
private:
	/// When non-empty the Executor is running in "seed" mode. The
	/// states in the map will be executed in an arbitrary order
	/// (outside the normal search interface) until they terminate. When
	/// the states reach a symbolic branch then either direction that
	/// satisfies one or more seeds will be added to this map. What
	/// happens with other states (that don't satisfy the seeds) depends
	/// on as-yet-to-be-determined flags.
	SeedCore	seedCore;
};
};

#endif
