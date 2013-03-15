#ifndef SYM_MMU_H
#define SYM_MMU_H

#include "MMU.h"

namespace klee
{
class KModule;
class KFunction;
class SoftMMUHandlers;
class SymMMU : public MMU
{
public:
	SymMMU(Executor& exe, const std::string& type);
	SymMMU(Executor& exe);
	virtual ~SymMMU(void);

	virtual bool exeMemOp(ExecutionState &state, MemOp& mop);
private:
	void initModule(Executor& exe, const std::string& type);
	SoftMMUHandlers	*mh;
};
}

#endif
