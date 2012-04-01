#ifndef BRANCHPREDICTORS_H
#define BRANCHPREDICTORS_H

#include "BranchPredictor.h"

namespace klee
{
class ExecutionState;
class KInstruction;

class RandomPredictor : public BranchPredictor
{
public:
	RandomPredictor();
	virtual ~RandomPredictor() {}
	virtual bool predict(
		const ExecutionState& st,
		KInstruction* ki,
		bool& hint);
private:
	unsigned phase_hint;
	int	period;
	int	period_bump;
};

class SeqPredictor : public BranchPredictor
{
public:
	SeqPredictor(const std::vector<bool>& v)
	: seq(v)
	, idx(0) {}
	~SeqPredictor() {}
	virtual bool predict(
		const ExecutionState& st,
		KInstruction* ki,
		bool& hint);
	static SeqPredictor* createTrue(void) { return createV(true); }
	static SeqPredictor* createFalse(void) { return createV(false); }
protected:
	static SeqPredictor* createV(bool b)
	{
		std::vector<bool>	v;
		v.push_back(b);
		return new SeqPredictor(v);
	}
private:
	std::vector<bool>	seq;
	int			idx;
};


class RotatingPredictor : public BranchPredictor
{
public:
	RotatingPredictor();
	virtual ~RotatingPredictor();
	virtual bool predict(
		const ExecutionState& st,
		KInstruction* ki,
		bool& hint);
	void add(BranchPredictor* bp) { bps.push_back(bp); }
private:
	unsigned	period;
	unsigned	tick;
	std::vector<BranchPredictor*> bps;
};


}
#endif
