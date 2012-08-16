#ifndef TAINTMERGECORE_H
#define TAINTMERGECORE_H

#include <map>
#include "klee/Expr.h"

namespace klee
{
class ExecutionState;
class Executor;
class ObjectState;
class MemoryObject;
class TaintMergeCombine;
class PTree;
class ExeStateManager;

/* func prefix, shadow tag */
typedef std::map<std::string,uint64_t>	tm_tags_ty;

class TaintMergeCore
{
public:
	TaintMergeCore(Executor* _exe);
	virtual ~TaintMergeCore() {}
	void setupInitialState(ExecutionState* es);
	void taintMergeBegin(ExecutionState& state);
	void taintMergeEnd(void);
private:
	void loadTags(const std::string& fname);
	Executor	*exe;
	tm_tags_ty	tm_tags;
	uint64_t	taint_id;

	/* state information before activating the branch mode */
	ExecutionState	*merging_st;
	PTree		*old_pt;
	ExeStateManager	*old_esm;
};
}

#endif