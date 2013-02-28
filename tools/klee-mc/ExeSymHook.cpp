#include <iostream>
#include "klee/Internal/Module/KModule.h"
#include <llvm/Function.h>
#include "ExeSymHook.h"
#include "ESVSymHook.h"
#include "klee/Common.h"
#include "../../lib/Core/KleeMMU.h"
#include "KleeHandlerVex.h"
#include "symbols.h"
#include "guest.h"

//#define DEBUG_ESH	1

using namespace klee;

#define es2esh(x)	static_cast<ESVSymHook&>(x)

class MallocMMU : public KleeMMU
{
public:
	MallocMMU(ExeSymHook& esh)
	: KleeMMU(esh), exe_esh(esh) {}

	virtual ~MallocMMU(void) {}
	virtual bool exeMemOp(ExecutionState &state, MemOp& mop)
	{
		is_write = mop.isWrite;
		return KleeMMU::exeMemOp(state, mop);
	}
protected:
	virtual MemOpRes memOpResolve(
		ExecutionState& state,
		ref<Expr> address,
		Expr::Width type);

private:
	ExeSymHook	&exe_esh;
	bool		is_write;
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

	ret = KleeMMU::memOpResolve(state, address, type);

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

	if (	!is_write &&
		esh.heapContains(addr & ~0xf, 1) &&
		esh.heapContains((addr+bytes-1) & ~0xf, 1))
	{
		klee_warning(
		"Permitting non-heap access at %p-%p (%p has 16-byte slack)",
		(void*)addr, (void*)(addr+bytes), (void*)(addr&~0xf));
		return ret;
	}

	/* neither blessed nor in the heap. bad access! */
	TERMINATE_ERROR_LONG(&exe_esh,
		state,
		"heap error: pointer neither blessed nor heap",
		"heap.err",
		exe_esh.getAddressInfo(state, address), false);

	ret.usable = false;
	ret.op.first = NULL;
	ret.rc = false;
	return ret;
}

ExeSymHook::ExeSymHook(InterpreterHandler *ie)
: ExecutorVex(ie)
, zero_malloc_ptr(0)
{
	assert (canSymhook(ie) == true);

	ExeStateBuilder::replaceBuilder(new ESVSymHookBuilder());

	memset(f_mallocs, 0, sizeof(f_mallocs));
	memset(f_frees, 0, sizeof(f_frees));
}

ExeSymHook::~ExeSymHook(void) {}


void ExeSymHook::run(ExecutionState &initialState)
{
	assert (mmu == NULL);
	mmu = new MallocMMU(*this);
	ExecutorVex::run(initialState);
}

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

	if (ret_ce == NULL) {
		ret_arg = toConstant(esh, ret_arg, "symbolic ret ptr");
		ret_ce = dyn_cast<ConstantExpr>(ret_arg);
	}
	assert (ret_ce != NULL);
	out_ptr = guest_ptr(ret_ce->getZExtValue());
	if (!out_ptr)
		return;

	if (in_len_ce == NULL) {
		in_len = toConstant(
			esh, ret_arg, "symbolic len")->getZExtValue();

	} else
		in_len = in_len_ce->getZExtValue();


#ifdef DEBUG_ESH
	std::cerr << "GOT PTR: " << (void*)out_ptr.o << '\n';
#endif

	if (in_len == 0) {
		if (zero_malloc_ptr == 0) {
			zero_malloc_ptr = out_ptr.o;
		}
		return;
	}

	llvm::Function	*f = esh.getWatchedFunc();
	if (	f == f_mallocs[FM_GI_LIBC_REALLOC] ||
		f == f_mallocs[FM_REALLOC])
	{
		esh.rmvHeapPtr(out_ptr.o);
	}

#ifdef DEBUG_ESH
	std::cerr << "GOT DATA: " << (void*)out_ptr.o << "-"
		<< (void*)(out_ptr.o + in_len) << '\n';
#endif
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
	if (in_ptr == zero_malloc_ptr)
		return;

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

#ifdef DEBUG_ESH
	std::cerr << "LEAVING: " << watch_f->getName().str() << "\n";
#endif
	esh.unwatch();
}

void ExeSymHook::watchFuncArg(
	ExecutionState& es, llvm::Function* f, ref<Expr>& in_arg)
{
	ESVSymHook		&esh(es2esh(es));
	uint64_t		stack_pos;

	if (isFreeFunc(f)) {
		const ConstantExpr*	in_ptr_ce;
		uint64_t		in_ptr;

		in_ptr_ce = dyn_cast<ConstantExpr>(in_arg);
		in_ptr = (in_ptr_ce == NULL)
			? 0
			: in_ptr_ce->getZExtValue();

#ifdef DEBUG_ESH
		std::cerr << "FREEING PTR: " << in_ptr << '\n';
#endif

		if (	in_ptr && in_ptr != zero_malloc_ptr && 
			!esh.hasHeapPtr(in_ptr))
		{
			TERMINATE_ERROR(this,
				esh,
				"heap error: freeing non-malloced pointer",
				"heapfree.err");
			return;
		}
	}

	stack_pos = getStateStack(es);
	if (!stack_pos)
		return;

#ifdef DEBUG_ESH
	std::cerr << "WATCHING: " << f->getName().str() << "\n";
#endif
	esh.enterWatchedFunc(f, in_arg, stack_pos);
}

void ExeSymHook::watchFunc(ExecutionState& es, llvm::Function* f)
{
	ref<Expr>		in_arg;

	if (!isWatchable(f))
		return;

	if (	f == f_mallocs[FM_MEMALIGN] ||
		f == f_mallocs[FM_GI_LIBC_REALLOC] ||
		f == f_mallocs[FM_REALLOC])
	{
		in_arg = getCallArg(es, 1);
	} else if (f == f_mallocs[FM_CALLOC] || f == f_mallocs[FM_CALLOC2]) {
		in_arg = MK_MUL(getCallArg(es, 0), getCallArg(es, 1));
	} else
		in_arg = getCallArg(es, 0);

	if (	dyn_cast<ConstantExpr>(in_arg) == NULL &&
		isMallocFunc(f))
	{
		/* TODO: exercise more malloc paths here */
		in_arg = toConstant(es, in_arg, "symbolic malloc", false);
	}

#ifdef DEBUG_ESH
	std::cerr << "Enter Watch: " << f->getName().str() << '\n';
#endif

	watchFuncArg(es, f, in_arg);
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

ExecutionState* ExeSymHook::setupInitialStateEntry(uint64_t entry)
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

		{NULL, NULL},
	};

	memset(f_mallocs, 0, sizeof(f_mallocs));
	memset(f_frees, 0, sizeof(f_frees));

	ret = ExecutorVex::setupInitialStateEntry(entry);
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


bool ExeSymHook::canSymhook(InterpreterHandler *ie)
{
	const Symbols	*syms;
	const Symbol	*sym_malloc;

	syms = dynamic_cast<KleeHandlerVex*>(ie)->getGuest()->getSymbols();
	if (syms == NULL)
		return false;

	sym_malloc = syms->findSym("malloc");
	if (!syms->findSym("malloc") && !syms->findSym("calloc"))
		return false;

	return true;
}