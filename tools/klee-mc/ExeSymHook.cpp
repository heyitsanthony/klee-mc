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
	/* if it's a symbolic address, pass it through */
	ce_addr = dyn_cast<ConstantExpr>(address);
	if (ce_addr == NULL)
		return ret;

	/* now we're finally checking for whether the address is OK */
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
, f_vasprintf(NULL), f_asprintf(NULL)
{
	ExeStateBuilder::replaceBuilder(new ESVSymHookBuilder());
	delete mmu;
	mmu = new MallocMMU(*this);

	memset(f_mallocs, 0, sizeof(f_mallocs));
	memset(f_frees, 0, sizeof(f_frees));
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
	guest_ptr		out_ptr;


	ret_arg = getRetArg(esh);
	ret_ce = dyn_cast<ConstantExpr>(ret_arg);
	in_len_ce = dyn_cast<ConstantExpr>(esh.getWatchParam());

	if (ret_ce == NULL || in_len_ce == NULL) {
		assert (0 == 1 && "Symbolic len/ret not yet supported");
		return;
	}

	in_len = in_len_ce->getZExtValue();
	if (in_len == 0)
		return;

	out_ptr = guest_ptr(ret_ce->getZExtValue());
	if (!out_ptr)
		return;

	llvm::Function	*f = esh.getWatchedFunc();
	if (	f == f_mallocs[FM_GI_LIBC_REALLOC] ||
		f == f_mallocs[FM_REALLOC])
	{
		esh.rmvHeapPtr(out_ptr.o);
	}

//	std::cerr << "GOT DATA: " << (void*)out_ptr.o << "-"
//		<< (void*)(out_ptr.o + in_len) << '\n';
	esh.addHeapPtr(out_ptr.o, in_len);
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
	if (isMallocFunc(watch_f)) {
		unwatchMalloc(esh);
	} else if (isFreeFunc(watch_f)) {
		unwatchFree(esh);
	} else {
		assert (0 == 1 && "WTF");
	}

//	std::cerr << "LEAVING: " << watch_f->getNameStr() << "\n";
	esh.unwatch();
}

void ExeSymHook::watchFunc(ExecutionState& es, llvm::Function* f)
{
	ESVSymHook		&esh(es2esh(es));
	ref<Expr>		in_arg;
	uint64_t		stack_pos;

	if (!isWatchable(f))
		return;

	if (	f == f_mallocs[FM_MEMALIGN] ||
		f == f_mallocs[FM_GI_LIBC_REALLOC] ||
		f == f_mallocs[FM_REALLOC])
	{
		in_arg = getCallArg(es, 1);
	} else if (f == f_mallocs[FM_CALLOC] || f == f_mallocs[FM_CALLOC2]) {
		in_arg = MulExpr::create(
			getCallArg(es, 0),
			getCallArg(es, 1));
	} else
		in_arg = getCallArg(es, 0);

	if (isFreeFunc(f)) {
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

//	std::cerr << "WATCHING: " << f->getNameStr() << "\n";
	esh.enterWatchedFunc(f, in_arg, stack_pos);
}

void ExeSymHook::sym2func(const Symbols* syms, sym2func_t* stab)
{
	for (int k = 0; stab[k].sym_name != NULL; k++) {
		const Symbol	*sym;

		sym = syms->findSym(stab[k].sym_name);
		if (!sym || *stab[k].f)
			continue;

		*(stab[k].f) = getFuncByAddr(sym->getBaseAddr());

		std::cerr
			<< "[ExeSymHook]  SETTING "
			<< stab[k].sym_name
			<< " TO: "
			<< (void*)sym->getBaseAddr() << '\n';
	}
}

bool ExeSymHook::isFreeFunc(llvm::Function* f) const
{
	if (!f) return false;

	for (int i = 0; i < FF_SIZE; i++)
		if (f_frees[i] == f)
			return true;

	return false;

}

bool ExeSymHook::isMallocFunc(llvm::Function* f) const
{
	if (!f) return false;

	for (int i = 0; i < FM_SIZE; i++)
		if (f_mallocs[i] == f)
			return true;
	return false;
}

bool ExeSymHook::isWatchable(llvm::Function* f) const
{
	if (isMallocFunc(f))
		return true;

	if (isFreeFunc(f))
		return true;

	return false;
}

ExecutionState* ExeSymHook::setupInitialState(void)
{
	ExecutionState	*ret;
	ESVSymHook	*esh;
	Guest		*gs;
	const Symbols	*syms;
	struct sym2func_t	symtab[] =
	{
		{"malloc", &f_mallocs[FM_MALLOC]},
		{"_int_malloc", &f_mallocs[FM_INT_MALLOC]},
		{"__GI___libc_malloc", &f_mallocs[FM_GI_LIBC_MALLOC]},
		{"memalign", &f_mallocs[FM_MEMALIGN]},
		{"__GI___libc_realloc", &f_mallocs[FM_GI_LIBC_REALLOC]},
		{"realloc", &f_mallocs[FM_REALLOC]},
		{"__calloc", &f_mallocs[FM_CALLOC2]},
		{"calloc", &f_mallocs[FM_CALLOC]},


		{"_int_free", &f_frees[FF_INT_FREE]},
		{"__GI___libc_free", &f_frees[FF_GI_LIBC_FREE]},
		{"free", &f_frees[FF_FREE]},

		{"vasprintf", &f_vasprintf},
		{"asprintf", &f_asprintf},
		{NULL, NULL},
	};


	ret = ExecutorVex::setupInitialState();
	esh = dynamic_cast<ESVSymHook*>(ret);
	assert (esh != NULL);

	gs = getGuest();

	syms = gs->getDynSymbols();
	assert (syms != NULL && "Can't hook without symbol names");
	sym2func(syms, symtab);


	syms = gs->getSymbols();
	assert (syms != NULL && "Can't hook without symbol names");
	sym2func(syms, symtab);

	assert (f_mallocs[FM_MALLOC] && "Could not decode hooked funcs");

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
	if (	!syms->findSym("malloc") &&
		!syms->findSym("calloc"))
	{
		return NULL;
	}

	return new ExeSymHook(ie, gs);
}