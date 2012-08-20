#ifndef TAINTMERGEEXECUTOR_H
#define TAINTMERGEEXECUTOR_H

#include "klee/Common.h"
#include "ExeStateManager.h"
#include "TaintMergeCore.h"
#include "../Expr/ShadowAlloc.h"

namespace klee
{

class ExecutionState;
class InterpreterHandler;

template<typename T>
class TaintMergeExecutor : public T
{
public:
	TaintMergeExecutor(InterpreterHandler* ie)
	: T(ie), tmCore(this) {}
	virtual ~TaintMergeExecutor() {}

	virtual void run(ExecutionState &initialState)
	{
		tmCore.setupInitialState(&initialState);
		T::run(initialState);
	}

	virtual void runLoop(void)
	{
		T::prevState = NULL;
		while ( !T::stateManager->empty() && !T::haltExecution) {
			tmCore.step();
			T::step();
		}
	}

#define TRY_TAINT_TERMINATE			\
	if (tmCore.isMerging()) {		\
		std::cerr << "TERMINATING??\n";			\
		T::printStackTrace(state, std::cerr);		\
		if (T::stateManager->isAddedState(&state))	\
			T::stateManager->commitQueue(NULL);	\
		T::stateManager->forceYield(&state);		\
		return;						\
	}

	void terminate(ExecutionState &state)
	{
		TRY_TAINT_TERMINATE
		T::terminate(state);
	}

	void terminateEarly(ExecutionState &state, const llvm::Twine &message)
	{
		TRY_TAINT_TERMINATE
		T::terminateEarly(state, message);
	}

	void terminateOnExit(ExecutionState &state)
	{
		TRY_TAINT_TERMINATE
		T::terminateOnExit(state);
	}

	void terminateOnError(
		ExecutionState &state,
		const llvm::Twine &msg,
		const char *suf,
		const llvm::Twine &longMsg="",
		bool alwaysEmit = false)
	{
		TRY_TAINT_TERMINATE
		T::terminateOnError(state, msg, suf, longMsg, alwaysEmit);
	}


protected:
	virtual void instBranchConditional(
		ExecutionState& state, KInstruction* ki)
	{
		ref<Expr>	cond(T::eval(ki, 0, state));

		if (tmCore.isMerging()) {
			/* XXX: what if it's a condition based on
			 * another taint group? UGH. */
			T::instBranchConditional(state, ki);
			return;
		}

		if (cond->isShadowed()) {
			std::cerr << "[TME] URk!!\n";
			std::cerr << "EXpr = " << cond << '\n';
			assert (0 == 1 && "XXXXXX");
		}
		T::instBranchConditional(state, ki);
	}

	virtual const ref<Expr> eval(
		KInstruction *ki,
		unsigned idx,
		ExecutionState &st) const
	{
		if (tmCore.isMerging()) {
			ref<Expr>	e;
			e = T::eval(ki, idx, st);
			/* do not over shadow */
			if (e->isShadowed() == false)
				e = e->realloc();
			return e;
		}
		return T::eval(ki, idx, st);
	}
private:

	TaintMergeCore	tmCore;
};
};

#endif
