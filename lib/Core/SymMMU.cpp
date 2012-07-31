#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Path.h>
#include "Executor.h"
#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KModule.h"
#include "SymMMU.h"

using namespace klee;

namespace klee { extern llvm::Module* getBitcodeModule(const char* path); }

namespace {
	llvm::cl::opt<std::string>
	SymMMUType(
		"sym-mmu-type",
		llvm::cl::desc("Suffix for symbolic MMU operations."),
		llvm::cl::init("uniqptr"));
};


KFunction	*SymMMU::f_store8 = NULL,
		*SymMMU::f_store16, *SymMMU::f_store32,
		*SymMMU::f_store64, *SymMMU::f_store128;

KFunction	*SymMMU::f_load8, *SymMMU::f_load16, *SymMMU::f_load32,
		*SymMMU::f_load64, *SymMMU::f_load128;

struct loadent
{
	const char*	le_name;
	KFunction**	le_kf;
};

void SymMMU::initModule(Executor& exe)
{
	KModule		*km(exe.getKModule());
	llvm::Module	*mod;
	std::string	suffix("_" + SymMMUType);

	struct loadent	loadtab[] =  {
		{ "mmu_load_8", &f_load8},
		{ "mmu_load_16", &f_load16},
		{ "mmu_load_32", &f_load32},
		{ "mmu_load_64", &f_load64},
		{ "mmu_load_128", &f_load128},
		{ "mmu_store_8", &f_store8},
		{ "mmu_store_16", &f_store16},
		{ "mmu_store_32", &f_store32},
		{ "mmu_store_64", &f_store64},
		{ "mmu_store_128", &f_store128},
		{ NULL, NULL}};

	/* already loaded? */
	if (f_store8 != NULL)
		return;

	llvm::sys::Path path(km->getLibraryDir());

	path.appendComponent("libkleeRuntimeMMU.bc");
	mod = getBitcodeModule(path.c_str());
	assert (mod != NULL);

	exe.addModule(mod);

	for (struct loadent* le = &loadtab[0]; le->le_name; le++) {
		std::string	func_name(le->le_name + suffix);
		KFunction	*kf(km->getKFunction(func_name.c_str()));
		if (kf == NULL) {
			std::cerr <<	"[SymMMU] Could not find: " <<
					func_name << '\n';
		}
		assert (kf != NULL);
		*(le->le_kf) = kf;
	}
}

SymMMU::SymMMU(Executor& exe)
: MMU(exe)
{ initModule(exe); }

bool SymMMU::exeMemOp(ExecutionState &state, MemOp& mop)
{
	KFunction		*f;
	Expr::Width		w(mop.getType(exe.getKModule()));
	std::vector<ref<Expr> >	args;

	if (mop.isWrite) {
		switch (w) {
		case 8:		f = f_store8; break;
		case 16:	f = f_store16; break;
		case 32:	f = f_store32; break;
		case 64:	f = f_store64; break;
		case 128:	f = f_store128; break;
		default:
			std::cerr << "[SymMMU] BAD WIDTH! W=" << w << '\n';
			assert (0 == 1 && "BAD WIDTH");
		}

		args.push_back(mop.address);
		if (w == 128) {
			/* ugh. coercion */
			args.push_back(MK_EXTRACT(mop.value, 0, 64));
			args.push_back(MK_EXTRACT(mop.value, 64, 64));
		} else
			args.push_back(mop.value);
		exe.executeCallNonDecl(state, f->function, args);

		sym_w_c++;
		return true;
	}

	switch(w) {
	case 8:		f = f_load8; break;
	case 16:	f = f_load16; break;
	case 32:	f = f_load32; break;
	case 64:	f = f_load64; break;
	case 128:	f = f_load128; break;
	default:
		assert (0 == 1 && "BAD WIDTH");
	}

	args.push_back(mop.address);
	exe.executeCallNonDecl(state, f->function, args);

	sym_r_c++;
	return true;
}
