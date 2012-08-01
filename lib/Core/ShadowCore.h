#ifndef SHADOWCORE_H
#define SHADOWCORE_H

namespace klee
{
class ExecutionState;
class Executor;
class ObjectState;
class MemoryObject;
class KInstruction;

/* func prefix, shadow tag */
typedef std::map<std::string,unsigned>	shadow_tags_ty;

class ShadowCore
{
public:
	ShadowCore(Executor* _exe);
	virtual ~ShadowCore() {}

	void setupInitialState(ExecutionState* es);
private:
	void loadShadowTags(const std::string& fname);

	Executor		*exe;
	shadow_tags_ty		shadow_tags;
};
}

#endif
