#include "ConcreteMMU.h"
#include "SoftConcreteMMU.h"
#include "SymMMU.h"
#include "KleeMMU.h"
#include "DualMMU.h"

using namespace klee;

DualMMU::DualMMU(Executor& exe)
: MMU(exe)
, mmu_conc(
	MMU::isSoftConcreteMMU()
	? (MMU*)new SoftConcreteMMU(exe)
	: (MMU*)new ConcreteMMU(exe))
, mmu_sym((MMU::isSymMMU())
	? (MMU*)new SymMMU(exe)
	: (MMU*)new KleeMMU(exe))
{}

DualMMU::DualMMU(Executor& exe, MMU* mmu_first, MMU* mmu_second)
: MMU(exe)
, mmu_conc(mmu_first)
, mmu_sym(mmu_second) {}


DualMMU::~DualMMU(void)
{
	delete mmu_sym;
	delete mmu_conc;
}

bool DualMMU::exeMemOp(ExecutionState &state, MemOp& mop)
{
	if (mmu_conc->exeMemOp(state, mop))
		return true;
	return mmu_sym->exeMemOp(state, mop);
}

DualMMU* DualMMU::create(MMU* normal_mmu, MMU* slow_mmu)
{
	Executor	&exe(normal_mmu->getExe());
	DualMMU*	dmmu = dynamic_cast<DualMMU*>(normal_mmu);

	if (dmmu != NULL) {
		/* keep fast path on top */
		dmmu->mmu_sym = new DualMMU(exe, slow_mmu, dmmu->mmu_sym);
		return dmmu;
	}

	return new DualMMU(exe, normal_mmu, slow_mmu);
}

void DualMMU::signal(ExecutionState& state, void* addr, uint64_t len)
{
	mmu_sym->signal(state, addr, len);
	mmu_conc->signal(state, addr, len);
}
