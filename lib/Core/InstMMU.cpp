#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Support/Path.h>
#include <llvm/Pass.h>
#include "Executor.h"
#include "klee/Internal/Module/KModule.h"
#include "InstMMU.h"
#include "static/Sugar.h"

using namespace klee;
using namespace llvm;

namespace klee { extern llvm::Module* getBitcodeModule(const char* path); }

KFunction	*InstMMU::f_store8 = NULL,
		*InstMMU::f_store16, *InstMMU::f_store32,
		*InstMMU::f_store64, *InstMMU::f_store128;

KFunction	*InstMMU::f_load8, *InstMMU::f_load16, *InstMMU::f_load32,
		*InstMMU::f_load64, *InstMMU::f_load128;

KFunction	*InstMMU::f_cleanup = NULL;

struct loadent
{
	const char*	le_name;
	KFunction**	le_kf;
	bool		le_required;
};


InstMMU::InstMMU(Executor& exe)
: KleeMMU(exe)
{ initModule(exe); }

class InstMMUPass : public llvm::FunctionPass
{
	static char ID;
public:
	virtual bool runOnFunction(llvm::Function &F);
	InstMMUPass() : llvm::FunctionPass(ID) {}
	virtual ~InstMMUPass() {}
private:
	bool replaceInst(Instruction *inst);
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
		switch (access_bits) {
		case 8:		kf = InstMMU::f_load8; break;
		case 16:	kf = InstMMU::f_load16; break;
		case 32:	kf = InstMMU::f_load32; break;
		case 64:	kf = InstMMU::f_load64; break;
		case 128:	kf = InstMMU::f_load128; break;
		default:
			std::cerr << "Bad load size=" << access_bits << '\n';
			assert ("STUB" && 0 == 1);
		}

		mmu_call = CallInst::Create(kf->function, v0);
		break;

	case Instruction::Store:
		switch (access_bits) {
		case 8:		kf = InstMMU::f_store8; break;
		case 16:	kf = InstMMU::f_store16; break;
		case 32:	kf = InstMMU::f_store32; break;
		case 64:	kf = InstMMU::f_store64; break;
		case 128:	kf = InstMMU::f_store128; break;
		default:
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

	ReplaceInstWithInst(
		inst->getParent()->getInstList(),
		ii,
		mmu_call);

	return true;
}


void InstMMU::initModule(Executor& exe)
{
	KModule		*km(exe.getKModule());
	llvm::Module	*mod;

	struct loadent	loadtab[] =  {
		{ "mmu_load_8_inst", &f_load8, true},
		{ "mmu_load_16_inst", &f_load16, true},
		{ "mmu_load_32_inst", &f_load32, true},
		{ "mmu_load_64_inst", &f_load64, true},
		{ "mmu_load_128_inst", &f_load128, true},
		{ "mmu_store_8_inst", &f_store8, true},
		{ "mmu_store_16_inst", &f_store16, true},
		{ "mmu_store_32_inst", &f_store32, true},
		{ "mmu_store_64_inst", &f_store64, true},
		{ "mmu_store_128_inst", &f_store128, true},
		{ "mmu_cleanup_inst", &f_cleanup, false},
		{ NULL, NULL, false}};

	/* already loaded? */
	if (f_store8 != NULL)
		return;

	llvm::sys::Path path(km->getLibraryDir());

	path.appendComponent("libkleeRuntimeMMU.bc");
	mod = getBitcodeModule(path.c_str());
	assert (mod != NULL);

	exe.addModule(mod);

	for (struct loadent* le = &loadtab[0]; le->le_name; le++) {
		KFunction	*kf(km->getKFunction(le->le_name));
		if (kf != NULL) {
			*(le->le_kf) = kf;
			continue;
		}
		if (le->le_required == false)
			continue;
		std::cerr << "[InstMMU] Couldn't find: " << le->le_name << '\n';
	}

	std::cerr << "[InstMMU] Online\n";

	if (f_cleanup != NULL)
		exe.addFiniFunction(f_cleanup->function);

	km->addFunctionPass(new InstMMUPass());
}
