#ifndef KTEST_EXE_H
#define KTEST_EXE_H

#include "../Core/Executor.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Expr.h"
#include "klee/Common.h"
#include "klee/Internal/ADT/KTest.h"

namespace klee
{
class ExecutionState;
class MemoryObject;
template<typename T>
class KTestExecutor : public T
{
public:
	KTestExecutor(InterpreterHandler* ie)
	: T(ie), replayKTest(0) {}

	virtual ~KTestExecutor() {}

	virtual bool isReplayKTest(void) const { return (replayKTest != NULL); }
	virtual void setReplayKTest(const struct KTest *out)
	{
		assert(!T::getReplay() && "cannot replay both ktest and path");
		replayKTest = out;
		replayPosition = 0;
	}

	virtual const struct KTest *getReplayKTest(void) const
	{ return replayKTest; }

	virtual void terminate(ExecutionState &state)
	{
		if (replayPosition != replayKTest->numObjects) {
			std::cerr << "[KTestExe] replayPosition = "
					<< replayPosition << '\n';
			std::cerr << "[KTestExe] numObjs = "
					<< replayKTest->numObjects << '\n';
			klee_warning_once(
				replayKTest,
				"replay did not consume all objects in test input.");
		}

		T::terminate(state);
	}

	/* seeds with ktest values */
	virtual ObjectState* makeSymbolic(
		ExecutionState& state,
		const MemoryObject* mo,
		const char* arrPrefix = "arr")
	{
		ObjectState *os = state.bindMemObjWriteable(mo);
		if (replayPosition >= replayKTest->numObjects) {
			T::terminateOnError(
				state, "replay count mismatch", "user.err");
			return os;
		}

		KTestObject *obj = &replayKTest->objects[replayPosition++];
		if (obj->numBytes != mo->size) {
			T::terminateOnError(
				state, "replay size mismatch", "user.err");
			return os;
		}

		for (unsigned i = 0; i < mo->size; i++) {
			state.write8(os, i, obj->bytes[i]);
		}

		return os;
	}

private:
	/// When non-null the bindings that will be used for calls to
	/// klee_make_symbolic in order replay.
	const struct KTest *replayKTest;

	/// The index into the current \ref replayKTest object.
	unsigned replayPosition;
};
}

#endif
