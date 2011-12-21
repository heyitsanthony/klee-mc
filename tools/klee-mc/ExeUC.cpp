#include <llvm/Support/CommandLine.h>
#include <assert.h>

#include "symbols.h"
#include "guestcpustate.h"

#include "static/Sugar.h"
#include "klee/Common.h"
#include "klee/Expr.h"
#include "klee/Internal/Module/KFunction.h"
#include "../../lib/Core/AddressSpace.h"

#include <iostream>
#include "klee/Solver.h"
#include "../../lib/Solver/SMTPrinter.h"

#include "ExeStateVex.h"
#include "ExeUC.h"
#include "UCMMU.h"

using namespace klee;

extern bool SymRegs;

namespace {
	llvm::cl::opt<unsigned>
	PtrTabLength(
		"ptrtab-len",
		llvm::cl::desc("Length of pointer table for unconstrained mode."),
		llvm::cl::init(1024));

}

ExeUC::ExeUC(InterpreterHandler *ie, Guest* gs)
: ExecutorVex(ie, gs)
{
	if (!SymRegs) {
		fprintf(stderr,
			"[EXEUC] Forcing Symregs. Use -symregs next time?\n");
		SymRegs = true;
	}

	delete mmu;
	mmu = new UCMMU(*this);
	lentab_reg_ptrs = gs->getCPUState()->getStateSize() / getPtrBytes();
	lentab_elem_len = (2*getPtrBytes())+4;
	lentab_max = lentab_reg_ptrs + PtrTabLength;
}

ExeUC::~ExeUC() {}

#define	SETUP_UC(es,idx,field_off,sz)		\
	unsigned		base_off;	\
	ref<Expr>		uc_expr;	\
	const ObjectState	*ros;		\
	base_off = idx*lentab_elem_len;		\
	ros = es.addressSpace.findObject(lentab_mo);	\
	assert (ros != NULL);				\
	uc_expr = es.read(ros, base_off+field_off, sz);


ref<Expr> ExeUC::getUCRealPtr(ExecutionState& es, unsigned idx)
{
	SETUP_UC(es, idx, REALPTR_OFF, getPtrBytes()*8)

	if (isa<ConstantExpr>(uc_expr))
		return uc_expr;

	return NULL;
}

ref<Expr> ExeUC::getUCSymPtr(ExecutionState& es, unsigned idx)
{
	SETUP_UC(es, idx, SYMPTR_OFF, getPtrBytes()*8)
	return uc_expr;
}

ref<Expr> ExeUC::getUCSize(ExecutionState& es, unsigned idx)
{
	SETUP_UC(es, idx, 0, 32)
	return uc_expr;
}

ExeUC::UCPtrFork ExeUC::forkUCPtr(
	ExecutionState		&es,
	MemoryObject		*new_mo,
	unsigned		idx)
{
	ObjectState	*wos;
	StatePair	res;
	unsigned	base_off;
	ref<Expr>	realptr_expr, len, min_size;

	new_mo->setName(std::string("uc_buf_") +  llvm::utostr(idx));

	realptr_expr = ConstantExpr::create(new_mo->address, getPtrBytes()*8);
	wos = es.addressSpace.findWriteableObject(lentab_mo);

	base_off = idx*lentab_elem_len;
	len = es.read(wos, base_off, 32);
	assert (!isa<ReadExpr>(len) && "len not symbolic?");

	/* 1. set real pointers to same value, */
	es.write(wos, base_off+REALPTR_OFF, realptr_expr);
	std::cerr << "HEY, FOUND NEW REALPTR: ";
	realptr_expr->dump();
	std::cerr << '\n';

	/* 2. fork into to cases: len <= static_sz and len > static_sz */
	min_size  = ConstantExpr::create(new_mo->size, 32);
	res = fork(es, UleExpr::create(len, min_size), true);

	if (res.first) {
		 /* 3.a for len <= sz,
		  * 	add constraint sym_ptr == real ptr, 
		  * 	add constraint len == sz */
		wos = res.first->addressSpace.findWriteableObject(lentab_mo);

		/* force len == sz */
		addConstraint(*res.first, EqExpr::create(len, min_size));

		/* force ptr == realptr */
		addConstraint(
			*res.first,
			EqExpr::create(
				getUCSymPtr(*res.first, idx),
				realptr_expr));

		res.first->write(wos, base_off, min_size);
		res.first->write(wos, base_off+SYMPTR_OFF, realptr_expr);
	}
	
	if (res.second) {
		 /* 3.b otherwise,
		  * 	leave sym_ptr symbolic
		  * 	(realptr was set prior to fork) */
		// assert (!isa<ConstantExpr>(getUCSymPtr(*res.second, idx)));
	}

	/* NOTE NOTE NOTE
	 * len <= static_sz will fault and die on overflows
	 * len > static_sz will catch the miss and silently extend its
	 * buffer to contain all misses (or complain about a large buffer).
	 */
	return UCPtrFork(res, realptr_expr);
}

ExeUC::UCPtrFork ExeUC::initUCPtr(
	ExecutionState& es, unsigned idx, unsigned min_sz)
{
	MemoryObject		*new_mo;

	/* 1. create a shared symbolic shadow object of at least 8 bytes */
	if (min_sz < 8)
		min_sz = 8;
	new_mo = memory->allocate(
		min_sz, false, true,
		es.getCurrentKFunc()->function,
		&es);
	executeMakeSymbolic(es, new_mo, "uc_buf");

	/* 2. expansion fork */
	return forkUCPtr(es, new_mo, idx);
}

void ExeUC::runImage(void)
{
	const char	*xchk_fn[] = {
		"strfry",
		"svc_find",
		"memcpy",
		"parse_dollars",
		"memset",
		"printf",
		"gettimeofday",
		NULL};
	
	for (unsigned i = 0; xchk_fn[i]; i++) {
		runSym(xchk_fn[i]);
	}

	fprintf(stderr, "DONE FOR THE DAY\n");
}

void ExeUC::runSym(const char* xchk_fn)
{
	ExecutionState	*start_state;
	const Symbols	*syms;
	const Symbol	*sym;


	fprintf(stderr, "[EXEUC] FINDING SYM: %s\n", xchk_fn);
	syms = gs->getSymbols();
	sym = syms->findSym(xchk_fn);

	assert (sym != NULL && "Couldn't find sym");

	start_state = setupInitialStateEntry(sym->getBaseAddr());
	if (start_state == NULL)
		return;

	setupUCEntry(start_state, xchk_fn);

	fprintf(stderr, "[EXEUC] RUNNING: %s\n", xchk_fn);

	run(*start_state);

	cleanupImage();
	fprintf(stderr, "[EXEUC] OK.\n");
}

unsigned ExeUC::getPtrBytes(void) const
{ return (gs->getMem()->is32Bit()) ? 4 : 8; }

void ExeUC::setupUCEntry(
	ExecutionState* start_state,
	const char *xchk_fn)
{
	ObjectState	*reg_os, *lentab_os;
	unsigned	ptr_bytes = getPtrBytes();
	uint64_t	off;

	/* restore stack pointer into symregs */
	reg_os = GETREGOBJ(*start_state);
	const char*  state_data = (const char*)gs->getCPUState()->getStateData();
	off = gs->getCPUState()->getStackRegOff();
	for (unsigned i=0; i < ptr_bytes; i++) {
		start_state->write8(reg_os, off+i, state_data[off+i]);
	}

	/* and restore the dflags; otherwise vex complains on get_rflags */
	assert (gs->getArch() == Arch::X86_64 && "STUPID DFLAG");
	off = 160; // offset of guest_DFLAG
	for (unsigned i=0; i < 8; i++) {
		start_state->write8(reg_os, off+i, state_data[off+i]);
	}

	root_reg_arr = reg_os->getArray();

	/* setup length table */
	lentab_mo = memory->allocate(
		lentab_max * lentab_elem_len,
		false, true,
		start_state->getCurrentKFunc()->function,
		start_state);
	lentab_mo->setName("lentab_mo");
	assert (lentab_mo != NULL);
	lentab_os = executeMakeSymbolic(*start_state, lentab_mo, "lentab");
	lentab_arr = lentab_os->getArray();

	fprintf(stderr, "[EXEUC] OFF THE DEEP END\n");
}

unsigned ExeUC::sym2idx(const Expr* sym_ptr) const
{
	const ConstantExpr	*ce;
	const ReadExpr		*re;
	const ConcatExpr	*cc;

	cc = dyn_cast<ConcatExpr>(sym_ptr);
	assert (cc != NULL);

	re = dyn_cast<ReadExpr>(cc->getKid(0));
	assert (re != NULL && "Not ReadExpr??");

	/* extract index from sym lookup to find real address */
	ce = dyn_cast<ConstantExpr>(re->index);
	assert (ce != NULL && "Expected constant index into ptrtab");

	return tabOff2Idx(ce->getZExtValue());
}

uint64_t ExeUC::getUCSym2Real(ExecutionState& es, ref<Expr> sym_ptr)
{
	const ConstantExpr	*ce;
	ref<Expr>		realptr;

	ce = dyn_cast<ConstantExpr>(sym_ptr);
	if (ce != NULL)
		return ce->getZExtValue();

	realptr = getUCRealPtr(es, sym2idx(sym_ptr.get()));
	assert (realptr.isNull() == false && "Realptr should exist by now");

	ce = dyn_cast<ConstantExpr>(realptr);
	return ce->getZExtValue();
}
