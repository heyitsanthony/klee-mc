#ifndef BRANCHPREDICTOR_H
#define BRANCHPREDICTOR_H

namespace klee
{
class ExecutionState;
class KInstruction;

class BranchPredictor
{
public:
	BranchPredictor() {}
	virtual ~BranchPredictor() {}
	virtual bool predict(
		const ExecutionState& st,
		KInstruction* ki,
		bool& hint) = 0;
private:
};
}
#endif
