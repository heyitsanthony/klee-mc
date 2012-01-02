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

	llvm::cl::opt<std::string>
	WithFuncName(
		"uc-func",
		llvm::cl::desc("Name of function to check with UC."));
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
	len = es.read(wos, base_off+LEN_OFF, 32);
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

		res.first->write(wos, base_off+LEN_OFF, min_size);
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

#define UC_LOWER_BOUND	0x50000000
#define UC_UPPER_BOUND	0xb0000000

ExecutionState* ExeUC::forkNullPtr(ExecutionState& es, unsigned pt_idx)
{
	StatePair	res;
	ref<Expr>	sym_ptr;
	ref<Expr>	cond_eq_null, cond_oob;

	sym_ptr = getUCSymPtr(es, pt_idx);

	/* 2. fork into to cases: len <= static_sz and len > static_sz */
	cond_eq_null = EqExpr::create(
		sym_ptr,
		ConstantExpr::create(0, sym_ptr->getWidth()));
	cond_oob = OrExpr::create(
		UltExpr::create(
			sym_ptr,
			ConstantExpr::create(
				UC_LOWER_BOUND, sym_ptr->getWidth())),
		UgtExpr::create(
			sym_ptr,
			ConstantExpr::create(
				UC_UPPER_BOUND, sym_ptr->getWidth())));

	res = fork(
		es,
		OrExpr::create(cond_eq_null, cond_oob),
		true);

	assert (res.first && res.second);

	terminateStateOnError(
		*res.first,
		"Unconstrained: Must be a pointer",
		"nullptr.err",
		getAddressInfo(*res.first, sym_ptr));

	return res.second;
}


ExeUC::UCPtrFork ExeUC::initUCPtr(
	ExecutionState& es, unsigned idx, unsigned min_sz)
{
	ExecutionState		*ptr_es;
	MemoryObject		*new_mo;

	/*
	 * 1. Since this is the initial dereference, we need to also fork off
	 * a state where there is only an invalid pointer--
	 * immediately terminate it
	 */
	ptr_es = forkNullPtr(es, idx);

	/* 2. create a shared symbolic shadow object of at least 8 bytes */
	if (min_sz < 8)
		min_sz = 8;
	new_mo = memory->allocate(
		min_sz, false, true,
		ptr_es->getCurrentKFunc()->function,
		ptr_es);
	executeMakeSymbolic(*ptr_es, new_mo, "uc_buf");

	/* 3. expansion fork */
	return forkUCPtr(*ptr_es, new_mo, idx);
}

static const char *xchk_fns[] = {
	"readtcp",
	"day_of_the_week",	// OK
	"getspent",		// negative residue
	"iruserok",		// negative residue
	"strfry",
	"svc_find",
	"memcpy",
	"parse_dollars",
	"memset",
	"printf",
	"gettimeofday",
	NULL};

void ExeUC::runImage(void)
{
	if (WithFuncName.size() != 0) {
		std::cerr << "USIGN FUNC: " << WithFuncName << '\n';
		runSym(WithFuncName.c_str());
	} else {
		for (unsigned i = 0; xchk_fns[i]; i++) {
			runSym(xchk_fns[i]);
			assert (0 == 1 && "KERPLUNK FIXME");
		}
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
	const char*	state_data;
	Exempts		ex(getRegExempts(gs));

	/* restore stack pointer into symregs */
	reg_os = GETREGOBJ(*start_state);
	state_data = (const char*)gs->getCPUState()->getStateData();
	foreach (it, ex.begin(), ex.end()) {
		unsigned	off, len;

		off = it->first;
		len = it->second;
		for (unsigned i=0; i < len; i++)
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
}

unsigned ExeUC::sym2idx(const Expr* sym_ptr) const
{
	const ConstantExpr	*ce;
	const ReadExpr		*re;
	const ConcatExpr	*cc;

	cc = dyn_cast<ConcatExpr>(sym_ptr);
	if (cc == NULL) {
		re = dyn_cast<ReadExpr>(sym_ptr);
	} else
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

void ExeUC::finalizeBuffers(ExecutionState& es)
{
	ObjectState*	lentab_os;

	lentab_os = es.addressSpace.findWriteableObject(lentab_mo);
	assert (lentab_os != NULL);

	for (unsigned idx = 0; idx < lentab_max; idx++) {
		ref<Expr>		realptr, len, symptr;
		const ConstantExpr	*ce;
		bool			ok;
		ObjectPair		res;
		unsigned		base_off;

		realptr = getUCRealPtr(es, idx);
		if (realptr.isNull())
			continue;

		ce = dyn_cast<ConstantExpr>(realptr);
		assert (ce != NULL);

		ok = es.addressSpace.resolveOne(ce->getZExtValue(), res);
		assert (ok && "REALPTR NOT FOUND");

		/* constrain length */
		base_off = idx*lentab_elem_len;
		len = es.read(lentab_os, base_off+LEN_OFF, 32);
		addConstraint(
			es,
			EqExpr::create(
				len,
				ConstantExpr::create(
					res.first->size,
					32)));

		/* constrain symptr */
		symptr = es.read(lentab_os, base_off+SYMPTR_OFF, getPtrBytes()*8);
		addConstraint(es, EqExpr::create(symptr, realptr));
	}
}
