#ifndef BRANCHPREDICTOR_H
#define BRANCHPREDICTOR_H

#include "klee/Expr.h"

namespace klee
{
class ExecutionState;
class KInstruction;

class BranchPredictor
{
public:
	class StateBranch {
	public:
		StateBranch(
			const ExecutionState	&_st,
			KInstruction		*_ki,
			const ref<Expr>		&_cond)
		: st(_st), ki(_ki), cond(_cond) {}

		const ExecutionState	&st;
		KInstruction		*ki;
		const ref<Expr>		cond;
	};
	BranchPredictor() {}
	virtual ~BranchPredictor() {}
	virtual bool predict(const StateBranch& sb, bool& hint) = 0;
private:
};
}
#endif
