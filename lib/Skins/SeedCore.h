#ifndef SEEDCORE_H
#define SEEDCORE_H

#include "../Core/SeedInfo.h"
#include <map>

namespace klee
{
class ExecutionState;
class Executor;
class ObjectState;
class MemoryObject;
class KInstruction;
class SeedCore
{
public:
	SeedCore(Executor* _exe) : exe(_exe), usingSeeds(NULL) {}
	virtual ~SeedCore() {}

	bool executeGetValueSeeding(
		ExecutionState &state,
		ref<Expr> e,
		KInstruction *target);

	void erase(ExecutionState* st);
	bool isInterestingTestCase(ExecutionState* st) const;

	void addSymbolicToSeeds(
		ExecutionState& state,
		const MemoryObject* mo,
		const  Array* array);
	bool seedRun(ExecutionState& initialState);
	void stepSeedInst(ExecutionState* &lastState);
	void useSeeds(const std::vector<struct KTest *> *s) { usingSeeds = s; }
	bool isUsingSeeds(void) const { return usingSeeds != NULL; }
	void checkAddConstraintSeeds(ExecutionState& state, ref<Expr> &cond);

	bool isStateSeeding(ExecutionState* s) const
	{ return (seedMap.find(s) != seedMap.end()); }

	bool isOnlySeed(void) const;
	SeedMapType& getSeedMap(void) { return seedMap; }
private:
	bool getSeedInfoIterRange(
		ExecutionState* s, SeedInfoIterator &b, SeedInfoIterator& e);
	bool seedObject(
		ExecutionState& state, SeedInfo& si,
		const MemoryObject* mo, const Array* array);


	Executor	*exe;
	SeedMapType	seedMap;

	/// When non-null a list of "seed" inputs which will be used to
	/// drive execution.
	const std::vector<struct KTest *> *usingSeeds;
};
}

#endif
