#ifndef SHADOWCORE_H
#define SHADOWCORE_H

#include <map>
#include "klee/Expr.h"

namespace klee
{
class ExecutionState;
class Executor;
class ObjectState;
class MemoryObject;
class KInstruction;
class ShadowMix;

/* func prefix, shadow tag */
typedef std::map<std::string,uint64_t>	shadow_tags_ty;

class ShadowCore
{
public:
	ShadowCore(Executor* _exe);
	virtual ~ShadowCore() {}

	void setupInitialState(ExecutionState* es);
	void addConstraint(ExecutionState &state, ref<Expr>& condition);

	static Executor* getExe(void) { return g_exe; }
private:
	void loadShadowTags(const std::string& fname);

	Executor		*exe;
	shadow_tags_ty		shadow_tags;
	ShadowMix		*sc;
	static Executor		*g_exe;
};
}

#endif
