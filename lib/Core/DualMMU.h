#ifndef DUAL_MMU_H
#define DUAL_MMU_H

#include "MMU.h"

namespace klee
{
class DualMMU : public MMU
{
public:
	DualMMU(Executor& exe);

	static DualMMU* create(MMU* normal_mmu, MMU* slow_mmu);

	bool exeMemOp(ExecutionState &state, MemOp& mop) override;
	void signal(ExecutionState& state, void* addr, uint64_t len) override;
protected:
	DualMMU(Executor& exe, MMU* fast_path, MMU* slow_path);
private:
	std::unique_ptr<MMU>	mmu_conc;
	std::unique_ptr<MMU>	mmu_sym;
};
}

#endif
