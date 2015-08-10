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

	bool exeMemOp(ExecutionState &state, MemOp& mop) override;
	void signal(ExecutionState& state, void* addr, uint64_t len) override;
private:
	void initModule(Executor& exe, const std::string& type);
	std::unique_ptr<SoftMMUHandlers>	mh;
};
}

#endif
