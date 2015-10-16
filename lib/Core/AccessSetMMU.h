#ifndef ACCESSSETMMU_H
#define ACCESSSETMMU_H

#include "MMU.h"
#include "klee/util/ExprHashMap.h"

namespace klee
{
class KInstruction;

class AccessSetMMU : public MMU
{
public:
	typedef std::unordered_map<KInstruction*, ExprHashSet>	ki2set_t;

	AccessSetMMU(MMU& mmu)
		: MMU(mmu.getExe())
		, base_mmu(mmu)
		, no_target_c(0)
	{}

	bool exeMemOp(ExecutionState &state, MemOp& mop) override;
	void signal(ExecutionState& state, void* addr, uint64_t len) override {
		base_mmu.signal(state, addr, len);
	}

	const ki2set_t& getAccesses() const { return ki2set; }

	void clear(void) { return ki2set.clear(); }

private:
	MMU		&base_mmu;	// does not delete base mmu!!
	ki2set_t	ki2set;
	unsigned	no_target_c;
};
}
#endif
