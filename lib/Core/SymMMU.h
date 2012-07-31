#ifndef SYM_MMU_H
#define SYM_MMU_H

#include "MMU.h"

namespace klee
{
class KModule;
class KFunction;

class SymMMU : public MMU
{
public:
	SymMMU(Executor& exe);
	virtual ~SymMMU(void) {}

	virtual bool exeMemOp(ExecutionState &state, MemOp& mop);
private:
	static void initModule(Executor& exe);
	static KFunction	*f_store8, *f_store16, *f_store32,
				*f_store64, *f_store128;

	static KFunction	*f_load8, *f_load16, *f_load32,
				*f_load64, *f_load128;
};
}

#endif
