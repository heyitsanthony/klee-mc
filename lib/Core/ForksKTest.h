#ifndef FORKSKTEST_H
#define FORKSKTEST_H

#include "Forks.h"

struct KTest;

namespace klee
{
class ForksKTest : public Forks
{
public:
	ForksKTest(Executor& exe)
	: Forks(exe), kt(0), kt_assignment(0) {}

	virtual ~ForksKTest();
	void setKTest(const KTest* _kt);
protected:
	bool setupForkAffinity(
		ExecutionState& current,
		struct ForkInfo& fi,
		unsigned* cond_idx_map);

	bool isBadOverflow(ExecutionState& current);
	virtual bool updateSymbolics(ExecutionState& current);
	virtual void addBinding(ref<Array>& a, std::vector<uint8_t>& v);
	const Assignment* getCurrentAssignment(void) { return kt_assignment; }
private:
	const KTest	*kt;
	Assignment	*kt_assignment;
	std::vector<ref<Array> > arrs;

};

class ForksKTestStateLogger : public ForksKTest
{
public:
	ForksKTestStateLogger(Executor& exe) : ForksKTest(exe) {}
	virtual ~ForksKTestStateLogger();
	ExecutionState* getNearState(const KTest* _kt);
protected:
	virtual bool updateSymbolics(ExecutionState& current);
	virtual void addBinding(ref<Array>& a, std::vector<uint8_t>& v);
private:
	typedef std::map<const Assignment, ExecutionState*> statecache_ty;
	typedef std::pair<unsigned, const std::string>	arrkey_ty;
	typedef std::map<arrkey_ty, ref<Array> >	arrcache_ty;

	arrcache_ty	arr_cache;
	statecache_ty	state_cache;
};
}

#endif
