#include <llvm/Support/CommandLine.h>

#include "ConcreteMMU.h"
#include "SymMMU.h"
#include "KleeMMU.h"
#include "DualMMU.h"

using namespace klee;


namespace {
	llvm::cl::opt<bool>
	UseSymMMU(
		"use-symmu",
		llvm::cl::desc("Use MMU that forwards to interpreter."),
		llvm::cl::init(false));
};

DualMMU::DualMMU(Executor& exe)
: MMU(exe)
, mmu_conc(new ConcreteMMU(exe))
, mmu_sym((UseSymMMU) ? (MMU*)new SymMMU(exe) : (MMU*)new KleeMMU(exe))
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
