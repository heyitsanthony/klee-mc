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

	virtual bool exeMemOp(ExecutionState &state, MemOp& mop);
private:
	MMU	*mmu_conc;
	MMU	*mmu_sym;
};
}

#endif
