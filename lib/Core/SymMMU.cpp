#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_os_ostream.h>
#include "Executor.h"
#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KModule.h"
#include "SoftMMUHandlers.h"
#include "SymMMU.h"

using namespace klee;

namespace klee { extern llvm::Module* getBitcodeModule(const char* path); }

namespace {
	llvm::cl::opt<std::string>
	SymMMUType(
		"sym-mmu-type",
		llvm::cl::desc("Suffix for symbolic MMU operations."),
		llvm::cl::init("objwide" /*uniqptr */));
};

void SymMMU::initModule(Executor& exe, const std::string& mmu_type)
{
	mh = new SoftMMUHandlers(exe, mmu_type);
	assert (mh != NULL);

	std::cerr << "[SymMMU] Using '" << mmu_type << "'\n";
	if (mh->getCleanup() != NULL)
		exe.addFiniFunction(mh->getCleanup()->function);

	/* XXX: add support for init */
}

SymMMU::SymMMU(Executor& exe)
: MMU(exe)
{ initModule(exe, SymMMUType); }

SymMMU::SymMMU(Executor& exe, const std::string& type)
: MMU(exe)
{ initModule(exe, type); }


SymMMU::~SymMMU(void) { delete mh; }

bool SymMMU::exeMemOp(ExecutionState &state, MemOp& mop)
{
	KFunction		*f;
	Expr::Width		w(mop.getType(exe.getKModule()));
	std::vector<ref<Expr> >	args;

	if (mop.isWrite) {
		f = mh->getStore(w);
		if (f == NULL) {
			std::cerr << "[SymMMU] BAD WIDTH! W=" << w << '\n';
			if (mop.target && mop.target->getInst())  {
				llvm::raw_os_ostream os(std::cerr);
				os << "[SymMMU] TARGET="
					<< *(mop.target->getInst()) << '\n'
					<< "[SymMMU] Func=\n"
					<< *(mop.target->getInst()->
						getParent()->getParent());
			}
			std::cerr << '\n';
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

	f = mh->getLoad(w);
	assert (f != NULL && "BAD WIDTH");

	args.push_back(mop.address);
	exe.executeCallNonDecl(state, f->function, args);

	sym_r_c++;
	return true;
}
