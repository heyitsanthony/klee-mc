#include <llvm/Support/CommandLine.h>
#include "Executor.h"
#include "SoftMMUHandlers.h"
#include "ConcreteMMU.h"
#include "klee/ExecutionState.h"
#include "klee/Internal/Module/KModule.h"

#include "SpecialFunctionHandler.h"
#include "SoftConcreteMMU.h"

using namespace klee;

namespace klee { extern llvm::Module* getBitcodeModule(const char* path); }

SoftConcreteMMU	*SoftConcreteMMU::singleton = NULL;

namespace {
	llvm::cl::opt<std::string>
	SConcMMUType(
		"sconc-mmu-type",
		llvm::cl::desc("Suffix for concrete MMU operations."),
		llvm::cl::init(""));
};

#define SFH_DEF_HANDLER(x)		\
void Handler##x::handle(		\
	ExecutionState	&state,		\
	KInstruction	*target,	\
	std::vector<ref<Expr> >& args)


#define MK_HI(name, h, ret)	\
SFH_HANDLER(h);			\
static struct SpecialFunctionHandler::HandlerInfo Hi##h = {	\
	name, &Handler##h::create, false, ret, false };		\
SFH_DEF_HANDLER(h)		\

MK_HI("klee_enable_softmmu", EnableSoftMMU, false)
{ state.isEnableMMU = true; }

#define SETUP_ADDRSZ()	\
	void		*addr;	\
	uint64_t	sz;	\
	if (	args[0]->getKind() != Expr::Constant &&	\
		args[1]->getKind() != Expr::Constant)	\
	{	\
		TERMINATE_ERROR(	\
			sfh->executor, state,	\
			"non-constant invalidate param", "user.err");	\
		return;	\
	}	\
	addr = (void*)(cast<ConstantExpr>(args[0])->getZExtValue(64));	\
	sz = cast<ConstantExpr>(args[1])->getZExtValue(64);


MK_HI("klee_tlb_insert", TLBInsert, false)
{
	SETUP_ADDRSZ();
	SoftConcreteMMU::get()->tlbInsert(state, addr, sz);
}

MK_HI("klee_tlb_invalidate", TLBInvalidate, false)
{
	SETUP_ADDRSZ();
	SoftConcreteMMU::get()->tlbInvalidate(state, addr, sz);
}

SoftConcreteMMU::SoftConcreteMMU(Executor& exe)
: SymMMU(exe, SConcMMUType)
{
	assert (singleton == NULL && "double allocate of soft concrete");
	exe.getSFH()->addHandler(HiEnableSoftMMU);
	exe.getSFH()->addHandler(HiTLBInsert);
	exe.getSFH()->addHandler(HiTLBInvalidate);
	cmmu = new ConcreteMMU(exe);
	singleton = this;
}

SoftConcreteMMU::~SoftConcreteMMU(void) { singleton = NULL; }

const std::string& SoftConcreteMMU::getType(void) { return SConcMMUType; }

bool SoftConcreteMMU::exeMemOp(ExecutionState &state, MemOp& mop)
{
	if (state.isEnableMMU == false)
		return cmmu->exeMemOp(state, mop);

	if (mop.address->getKind() != Expr::Constant)
		return false;

	ObjectPair	op;
	uint64_t	addr;

	addr = cast<ConstantExpr>(mop.address)->getZExtValue();
	if (!state.stlb.get(state, addr, op))
		goto slowpath;

	/* the objectstate pointer may be NULL if there
	 * was an address space generation update */
	if (op.second == NULL) {
		/* MO is still valid though.. */
		op.second = state.addressSpace.findObject(op.first);
	}

	/* XXX: I don't like calling this range check here */
	if ((mop.getType(exe.getKModule())/8 + op.first->getOffset(addr))
		<= op.second->size)
	{
		cmmu->commitMOP(state, mop, op, addr);
		return true;
	}

	/* slow path into the runtime */
slowpath:
	state.isEnableMMU = false;
	return SymMMU::exeMemOp(state, mop);
}

void SoftConcreteMMU::tlbInsert(
	ExecutionState& st, const void* addr, uint64_t len)
{
	if (len != TLB_PAGE_SZ) {
		std::cerr << "[SoftConcreteMMU] Bad TLB insert addr="
			<< addr << ". len=" << len << '\n';
		TERMINATE_ERROR(&getExe(), st, "Bad TLB insert",  "user.err");
		return;
	}

	ObjectPair	op;
	if (!cmmu->lookup(st, (uint64_t)addr, 1, op)) {
		std::cerr << "[SoftConcreteMMU] Bad TLB lookup addr="
			<< addr << ". len=" << len << '\n';
		TERMINATE_ERROR(&getExe(), st, "Bad TLB lookup",  "user.err");
		return;
	}
	
	st.stlb.put(st, op);
}

void SoftConcreteMMU::tlbInvalidate(
	ExecutionState& st, const void* addr, uint64_t len)
{ st.stlb.invalidate((uint64_t)addr); }