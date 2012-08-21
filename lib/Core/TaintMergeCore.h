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
class TaintGroup;
class TaintUpdateAction;

/* func prefix, shadow tag */
typedef std::map<std::string,uint64_t>	tm_tags_ty;

typedef uint64_t TaintID;
typedef std::map<TaintID, TaintGroup*>	taintgrps_ty;

class TaintMergeCore
{
public:
	TaintMergeCore(Executor* _exe);
	virtual ~TaintMergeCore();
	void setupInitialState(ExecutionState* es);
	void taintMergeBegin(ExecutionState& state);
	void taintMergeEnd(void);
	bool isMerging(void) const { return merging; }
	void step(void);
	void addConstraint(ExecutionState &state, ref<Expr> condition);
private:
	void loadTags(const std::string& fname);
	Executor	*exe;
	tm_tags_ty	tm_tags;
	uint64_t	cur_taint_id;
	taintgrps_ty	taint_grps;

	/* state information before activating the branch mode */
	ExecutionState	*merging_st;
	ExeStateManager	*old_esm;
	TaintUpdateAction	*tua;
	unsigned		merge_depth;
	bool			merging;
	bool			was_quench;
	static unsigned	nested_merge_c;
	static unsigned merges_c;
	static unsigned merge_states_c;
};
}

#endif