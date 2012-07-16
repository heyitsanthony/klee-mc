#ifndef SYM_MMU_H
#define SYM_MMU_H

#include "MMU.h"

namespace klee
{
class SymMMU : public MMU
{
public:
	SymMMU(Executor& exe) : MMU(exe) {}
	virtual ~SymMMU(void) {}

	virtual bool exeMemOp(ExecutionState &state, MemOp& mop);
private:
};
}

#endif
