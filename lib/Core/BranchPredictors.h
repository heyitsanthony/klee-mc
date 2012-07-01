#ifndef BRANCHPREDICTORS_H
#define BRANCHPREDICTORS_H

#include "static/Sugar.h"
#include <vector>
#include "BranchPredictor.h"

namespace klee
{
class ExecutionState;
class KInstruction;
class Forks;

class ListPredictor : public BranchPredictor
{
public:
	ListPredictor() {}
	virtual ~ListPredictor()
	{ foreach (it, bps.begin(), bps.end()) delete *it; }
	void add(BranchPredictor* bp) { bps.push_back(bp); }
	virtual bool predict(const StateBranch& sb, bool& hint)
	{
		foreach (it, bps.begin(), bps.end())
			if ((*it)->predict(sb, hint))
				return true;
		return false;
	}
private:
	std::vector<BranchPredictor*> bps;
};


class ExprBiasPredictor : public BranchPredictor
{
public:
	ExprBiasPredictor(void) {}
	virtual ~ExprBiasPredictor() {}
	virtual bool predict(const StateBranch& sb, bool& hint);
private:
};

class RandomPredictor : public BranchPredictor
{
public:
	RandomPredictor();
	virtual ~RandomPredictor() {}
	virtual bool predict(const StateBranch& sb, bool& hint);
private:
	unsigned phase_hint;
	int	period;
	int	period_bump;
};

class KBrPredictor: public BranchPredictor
{
public:
	KBrPredictor() {}
	virtual ~KBrPredictor() {}
	virtual bool predict(const StateBranch& sb, bool& hint);
private:
};

class SeqPredictor : public BranchPredictor
{
public:
	SeqPredictor(const std::vector<bool>& v)
	: seq(v)
	, idx(0) {}
	~SeqPredictor() {}
	virtual bool predict(const StateBranch& sb, bool& hint);
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
	virtual bool predict(const StateBranch& sb, bool& hint);
	void add(BranchPredictor* bp) { bps.push_back(bp); }
private:
	unsigned	period;
	unsigned	tick;
	std::vector<BranchPredictor*> bps;
};

class CondPredictor : public BranchPredictor
{
public:
	CondPredictor(const Forks* _f) : f(_f)  {}
	virtual ~CondPredictor() {}
	virtual bool predict(const StateBranch& sb, bool& hint);
private:
	const Forks*	f;
};
}
#endif
