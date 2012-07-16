#include "ConcreteMMU.h"
#include "SymMMU.h"
#include "KleeMMU.h"
#include "DualMMU.h"

using namespace klee;

DualMMU::DualMMU(Executor& exe)
: MMU(exe)
, mmu_conc(new ConcreteMMU(exe))
//, mmu_sym(new SymMMU(exe))
, mmu_sym(new KleeMMU(exe))
{}

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
