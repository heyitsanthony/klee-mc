#ifndef SOFTCONCRETEMMU_H
#define SOFTCONCRETEMMU_H

#include "TLB.h"
#include "SymMMU.h"

namespace klee
{
class ConcreteMMU;

class SoftConcreteMMU : public SymMMU
{
public:
	SoftConcreteMMU(Executor& exe);
	virtual ~SoftConcreteMMU(void);

	bool exeMemOp(ExecutionState &state, MemOp& mop) override;

	void tlbInsert(ExecutionState& state, const void* addr, uint64_t len);
	void tlbInvalidate(
		ExecutionState& state, const void* addr, uint64_t len);

	static SoftConcreteMMU* get(void) { return singleton; }
	static const std::string& getType(void);
private:
	void initModule(Executor& exe);
	static SoftConcreteMMU	*singleton;
	ConcreteMMU		*cmmu;
};
}

#endif
