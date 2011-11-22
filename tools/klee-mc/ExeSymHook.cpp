#include <iostream>
#include "klee/Internal/Module/KModule.h"
#include <llvm/Function.h>
#include "ExeSymHook.h"
#include "ESVSymHook.h"
#include "../../lib/Core/MMU.h"
#include "symbols.h"
#include "guest.h"


using namespace klee;

#define es2esh(x)	static_cast<ESVSymHook&>(x)

class MallocMMU : public MMU
{
public:
	MallocMMU(ExeSymHook& esh)
	: MMU(esh), exe_esh(esh) {}

	virtual ~MallocMMU(void) {}

protected:
	virtual MemOpRes memOpResolve(
		ExecutionState& state,
		ref<Expr> address,
		Expr::Width type);

private:
	ExeSymHook	&exe_esh;
};

MallocMMU::MemOpRes MallocMMU::memOpResolve(
	ExecutionState& state,
	ref<Expr> address,
	Expr::Width type)
{
	MemOpRes		ret;
	const ConstantExpr	*ce_addr;
	uint64_t		addr, bytes;
	ESVSymHook		&esh(es2esh(state));

	ret = MMU::memOpResolve(state, address, type);

	/* failed to resolve, no need to do another check */
	if (!ret.usable || !ret.rc)
		return ret;

	/* watched functions can touch whatever..
	 * the heap manager can mess with non-heap memory */
	if (esh.isWatched())
		return ret;

	/* right now we only check for concrete heap addresses */
	ce_addr = dyn_cast<ConstantExpr>(address);
	if (ce_addr == NULL)
		return ret;
	addr = ce_addr->getZExtValue();

	bytes = (type + 7) / 8;
	if (esh.isBlessed(ret.mo))
		return ret;

	if (esh.heapContains(addr, bytes))
		return ret;

	/* neither blessed nor in the heap. bad access! */
	exe_esh.terminateStateOnError(
		state,
		"heap error: pointer neither blessed nor heap",
		"heap.err",
		exe_esh.getAddressInfo(state, address));

	ret.usable = false;
	ret.op.first = NULL;
	ret.rc = false;
	return ret;
}

ExeSymHook::ExeSymHook(InterpreterHandler *ie, Guest* gs)
: ExecutorVex(ie, gs)
, f_malloc(NULL)
, f_free(NULL)
{
	ExeStateBuilder::replaceBuilder(new ESVSymHookBuilder());
	delete mmu;
	mmu = new MallocMMU(*this);
}

ExeSymHook::~ExeSymHook(void) {}

void ExeSymHook::executeCall(
	ExecutionState &state,
	KInstruction *ki,
	llvm::Function *f,
	std::vector< ref<Expr> > &arguments)
{
	watchEnterXfer(state, f);
	ExecutorVex::executeCall(state, ki, f, arguments);
}

void ExeSymHook::watchEnterXfer(ExecutionState& es, llvm::Function* f)
{
	ESVSymHook	&esh(es2esh(es));

	if (esh.isWatched())
		return;
	watchFunc(es, f);
}

void ExeSymHook::jumpToKFunc(ExecutionState& state, KFunction* kf)
{
	ESVSymHook	&esh(es2esh(state));

	if (!esh.isWatched()) {
		watchEnterXfer(state, kf->function);
	} else {
		// if (esh.isWatched())
		/* check the water mark, if the stack is greater than
		 * the watermark (e.g. closer to the beginning of stack mem),
		 * then we have returned from the watched function */
		if (getStateStack(state) > esh.getWatermark())
			unwatch(esh);
	}

	ExecutorVex::jumpToKFunc(state, kf);
}

void ExeSymHook::unwatchMalloc(ESVSymHook &esh)
{
	const ConstantExpr	*ret_ce;
	ref<Expr>		ret_arg;
	const ConstantExpr*	in_len_ce;
	unsigned int		in_len;


	ret_arg = getRetArg(esh);
	ret_ce = dyn_cast<ConstantExpr>(ret_arg);
	in_len_ce = dyn_cast<ConstantExpr>(esh.getWatchParam());

	if (ret_ce == NULL || in_len_ce == NULL) {
		assert (0 == 1 && "Symbolic len/ret not yet supported");
		return;
	}

	in_len = in_len_ce->getZExtValue();
	esh.addHeapPtr(ret_ce->getZExtValue(), in_len);
}

void ExeSymHook::unwatchFree(ESVSymHook &esh)
{
	const ConstantExpr*	in_ptr_ce;
	uint64_t		in_ptr;

	in_ptr_ce = dyn_cast<ConstantExpr>(esh.getWatchParam());
	if (in_ptr_ce == NULL)
		return;

	in_ptr = in_ptr_ce->getZExtValue();
	esh.rmvHeapPtr(in_ptr);
}

void ExeSymHook::unwatch(ESVSymHook &esh)
{
	llvm::Function		*watch_f;

	watch_f = esh.getWatchedFunc();
	if (watch_f == f_malloc) {
		unwatchMalloc(esh);
	} else if (watch_f == f_free) {
		unwatchFree(esh);
	} else {
		assert (0 == 1 && "WTF");
	}

	esh.unwatch();
}

void ExeSymHook::watchFunc(ExecutionState& es, llvm::Function* f)
{
	ESVSymHook		&esh(es2esh(es));
	ref<Expr>		in_arg;
	uint64_t		stack_pos;

	if (f != f_malloc && f != f_free)
		return;

	in_arg = getCallArg(es, 0);

	if (f == f_free) {
		const ConstantExpr*	in_ptr_ce;
		uint64_t		in_ptr;

		in_ptr_ce = dyn_cast<ConstantExpr>(in_arg);
		in_ptr = (in_ptr_ce == NULL)
			? 0
			: in_ptr_ce->getZExtValue();

		if (in_ptr && !esh.hasHeapPtr(in_ptr)) {
			terminateStateOnError(
				esh,
				"heap error: freeing non-malloced pointer",
				"heapfree.err");
			return;
		}
	}



	stack_pos = getStateStack(es);
	if (!stack_pos)
		return;

	esh.enterWatchedFunc(f, in_arg, stack_pos);
}

ExecutionState* ExeSymHook::setupInitialState(void)
{
	ExecutionState	*ret;
	ESVSymHook	*esh;
	Guest		*gs;
	const Symbols	*syms;
	const Symbol	*sym_malloc, *sym_free;


	ret = ExecutorVex::setupInitialState();
	esh = dynamic_cast<ESVSymHook*>(ret);
	assert (esh != NULL);

	gs = getGuest();
	syms = gs->getDynSymbols();
	assert (syms != NULL && "Can't hook without symbol names");

	sym_malloc = syms->findSym("malloc");
	sym_free = syms->findSym("free");

	assert (sym_malloc && "Could not finds syms to hook");

	f_malloc = getFuncByAddr(sym_malloc->getBaseAddr());
	if (sym_free)
		f_free = getFuncByAddr(sym_free->getBaseAddr());

	assert (f_malloc && "Could not decode hooked funcs");

	return ret;
}


ExeSymHook* ExeSymHook::create(InterpreterHandler *ie, Guest* gs)
{
	const Symbols	*syms;
	const Symbol	*sym_malloc;

	syms = gs->getDynSymbols();
	if (syms == NULL)
		return NULL;

	sym_malloc = syms->findSym("malloc");
	if (sym_malloc == NULL)
		return NULL;

	return new ExeSymHook(ie, gs);
}