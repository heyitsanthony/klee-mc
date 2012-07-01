#ifndef CONSTRAINT_SEED_EXE_H
#define CONSTRAINT_SEED_EXE_H

#include "klee/Expr.h"
#include "ConstraintSeedCore.h"

namespace klee
{
class ExecutionState;
class MemoryObject;
template<typename T>
class ConstraintSeedExecutor : public T
{
public:
	ConstraintSeedExecutor(InterpreterHandler* ie)
	: T(ie), csCore(this) {}
	virtual ~ConstraintSeedExecutor() {}

	virtual ObjectState* makeSymbolic(
		ExecutionState& state,
		const MemoryObject* mo,
		ref<Expr> len,
		const char* arrPrefix = "arr")
	{
		ObjectState*	os(T::makeSymbolic(state, mo, len, arrPrefix));
		if (os == NULL)
			return NULL;

		csCore.addSeedConstraints(state, os->getArrayRef());
		return os;
	}
private:
	ConstraintSeedCore	csCore;
};
}
#endif
