#ifndef DUAL_MMU_H
#define DUAL_MMU_H

#include "MMU.h"

namespace klee
{
class DualMMU : public MMU
{
public:
	DualMMU(Executor& exe);

	virtual ~DualMMU(void);

	static DualMMU* create(MMU* normal_mmu, MMU* slow_mmu);

	virtual bool exeMemOp(ExecutionState &state, MemOp& mop);
	virtual void signal(ExecutionState& state, void* addr, uint64_t len);
protected:
	DualMMU(Executor& exe, MMU* fast_path, MMU* slow_path);
private:
	MMU	*mmu_conc;
	MMU	*mmu_sym;
};
}

#endif
