#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Pass.h>
#include "Executor.h"
#include "klee/Internal/Module/KModule.h"
#include "InstMMU.h"
#include "SoftMMUHandlers.h"
#include "static/Sugar.h"

using namespace klee;
using namespace llvm;

class InstMMUPass : public llvm::FunctionPass
{
	static char ID;
public:
	virtual bool runOnFunction(llvm::Function &F);
	InstMMUPass(const SoftMMUHandlers* _mh)
	: llvm::FunctionPass(ID)
	, mh(_mh) {}
	virtual ~InstMMUPass() {}
private:
	bool replaceInst(Instruction *inst);
	const SoftMMUHandlers	*mh;
};
char InstMMUPass::ID;

bool InstMMUPass::runOnFunction(llvm::Function &F)
{
	bool	changed = false;

	foreach (bit, F.begin(), F.end()) {
		for (	BasicBlock::iterator i = bit->begin(), ie = bit->end();
			i != ie;)
		{
			Instruction	*inst = &*i;
			i++;
			changed |= replaceInst(inst);
		}
	}

	return changed;
}

bool InstMMUPass::replaceInst(Instruction *inst)
{
	BasicBlock::iterator	ii(inst);
	Type			*ty;
	Value			*v0;
	KFunction		*kf;
	CallInst		*mmu_call;
	unsigned		access_bits;
	Value			*args[2];

	v0 = (inst->getNumOperands() > 0) ? inst->getOperand(0) : NULL;
	ty = inst->getType();

	if (inst->getNumOperands() == 2)
		/* store */
		access_bits =  v0->getType()->getPrimitiveSizeInBits();
	else
		/* load */
		access_bits = ty->getPrimitiveSizeInBits();

	kf = NULL;
	switch (inst->getOpcode()) {
	case Instruction::Load:
		kf = mh->getLoad(access_bits);
		if (kf == NULL) {
			std::cerr << "Bad load size=" << access_bits << '\n';
			assert ("STUB" && 0 == 1);
		}

		mmu_call = CallInst::Create(kf->function, v0);
		break;

	case Instruction::Store:
		kf = mh->getStore(access_bits);
		if (kf == NULL) {
			std::cerr << "Bad store size=" << access_bits << '\n';
			assert ("STUB" && 0 == 1);
		}

		args[0] = inst->getOperand(1); /* ptr */
		args[1] = inst->getOperand(0); /* val */
		mmu_call = CallInst::Create(
			kf->function, ArrayRef<Value*>(args, 2));
		break;
	default:
		return false;
	}

	ReplaceInstWithInst(inst->getParent()->getInstList(), ii, mmu_call);
	return true;
}

InstMMU::InstMMU(Executor& exe)
: KleeMMU(exe)
{
	mh = new SoftMMUHandlers(exe, "inst");

	std::cerr << "[InstMMU] Online\n";

	if (mh->getCleanup() != NULL)
		exe.addFiniFunction(mh->getCleanup()->function);

	exe.getKModule()->addFunctionPass(new InstMMUPass(mh));
}


InstMMU::~InstMMU(void) { delete mh; }


