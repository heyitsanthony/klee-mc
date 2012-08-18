/**
 * MOTHER OF GOD
 */
#include "../../lib/Core/AddressSpace.h"
#include "../../lib/Core/MemoryManager.h"
#include "../../lib/Core/StateSolver.h"
#include "klee/Internal/Module/KModule.h"
#include "klee/util/ExprUtil.h"

#include "ExeStateVex.h"
#include "UCMMU.h"

using namespace klee;


class RepairVisitor : public ExprVisitor
{
private:
	ExeUC		&exe_uc;
	ExecutionState	&state;

	bool		aborted;
	unsigned	ptr_sz;

public:
	ptrtab_map		ptrtab_idxs;
	std::set<ref<Expr> >	untracked;

	RepairVisitor(ExeUC& in_uc, ExecutionState& es)
	: ExprVisitor(true)
	, exe_uc(in_uc)
	, state(es)
	{ ptr_sz = exe_uc.getPtrBytes(); }

	ref<Expr> repair(ref<Expr>& e);
	virtual Action visitExpr(const Expr &e);
	int getRootPtrIdx(const ReadExpr* re);
};

/* on success, all symbolic pointers rewritten to their realpointers */
ref<Expr> RepairVisitor::repair(ref<Expr>& e)
{
	ref<Expr>	realptr;

	ptrtab_idxs.clear();
	aborted = false;

	realptr = visit(e);
	if (aborted)
		return NULL;

	return realptr;
}

RepairVisitor::Action RepairVisitor::visitExpr(const Expr &e)
{
	ref<Expr>		real_ptr;
	const ReadExpr		*re;
	unsigned		re_idx;
	const ref<Array>	re_arr;
	int			pt_idx;

	if (aborted)
		return Action::skipChildren();

	re = dyn_cast<const ReadExpr>(&e);
	if (re == NULL)
		return Action::doChildren();

	re_arr = re->getArray();
	if (*re_arr == *exe_uc.getRootArray()) {
		pt_idx = getRootPtrIdx(re);
	} else if (*re_arr == *exe_uc.getPtrTabArray()) {
		pt_idx = exe_uc.sym2idx(&e);
	} else {
		untracked.insert(ref<Expr>(static_cast<Expr*>((ReadExpr*)re)));
		return Action::doChildren();
	}

	ptrtab_idxs[pt_idx] = ref<Expr>(static_cast<Expr*>((ReadExpr*)re));
	real_ptr = exe_uc.getUCRealPtr(state, pt_idx);
	if (real_ptr.isNull()) {
		return Action::doChildren();
	}

	assert (real_ptr.isNull() == false);

	/* must be constantexpr!! */
	re_idx = dyn_cast<ConstantExpr>(re->index)->getZExtValue();
	return Action::changeTo(
		ExtractExpr::create(real_ptr, 8*(re_idx%ptr_sz), 8));
}

int RepairVisitor::getRootPtrIdx(const ReadExpr* re)
{
	const ConstantExpr	*ce;
	unsigned		ptab_idx;
	ref<Expr>		realptr;

	ce = dyn_cast<ConstantExpr>(re->index);
	assert (ce != NULL && "Non-const regfile access");

	ptab_idx = ce->getZExtValue();
	ptab_idx /= exe_uc.getPtrBytes();

	return ptab_idx;
}

////////////////////////////////////////////////////////////////////////////

UCMMU::UCRewrite UCMMU::rewriteAddress(
	ExecutionState& state, ref<Expr> address)
{
	UCRewrite	ret;
	RepairVisitor	rv(exe_uc, state);

	assert (!isa<ConstantExpr>(address));

	std::cerr << "TRYING TO REPAIR: ";
	address->dump();
	std::cerr<<'\n';

	ret.old_expr = address;
	ret.new_expr = rv.repair(address);
	ret.ptrtab_idxs = rv.ptrtab_idxs;
	ret.untracked = rv.untracked;

	std::cerr << "NEW REPAIR:";
	ret.new_expr->dump();
	std::cerr<<'\n';

	return ret;
}


UCMMU::MemOpRes UCMMU::memOpResolve(
	ExecutionState& state,
	ref<Expr> address,
	Expr::Width type)
{
	MemOpRes	ret;

	if (aborted)
		return MemOpRes::failure();

	ret = KleeMMU::memOpResolve(state, address, type);

	/*  resolved fine or constant, no need to do another check */
	if ((ret.usable && ret.rc) || isa<ConstantExpr>(address))
		return ret;

	resteer = rewriteAddress(state, address);
	resteered = true;
	if (isa<ConstantExpr>(resteer.new_expr))
		return KleeMMU::memOpResolve(state, resteer.new_expr, type);

	return MemOpRes::failure();
}


/* error message on failure, updates res on success with expanded MO */
const char* UCMMU::expandRealPtr(
	ExecutionState& state,
	uint64_t base_ptr,
	ref<Expr> full_ptr,
	ObjectPair& res)
{
	StateSolver		*s;
	ref<Expr>		cond, sym_off;
	ObjectPair		ret;
	unsigned		resize_len;
	bool			sat;

	/* double length of array until we hit the right size */
	/* TODO: binary search after hitting upper bound to find
	 * tightest buffer possible */
	s = exe_uc.getSolver();
	sat = false;

	sym_off = SubExpr::create(
		full_ptr,
		ConstantExpr::create(
			base_ptr, full_ptr->getWidth()));
	resize_len = res.first->size/2;
	do {
		bool	ok;

		resize_len *= 2;
		cond = UleExpr::create(
			sym_off,
			ConstantExpr::create(resize_len, sym_off->getWidth()));
		ok = s->mayBeTrue(state, cond, sat);
		if (!ok)
			return "Unconstrained: Expand query failed";

	} while (sat == false && resize_len <= MAX_EXPAND_SIZE);

	if (resize_len >= MAX_EXPAND_SIZE) {
		std::cerr << ":-( RESIZE LEN = " << resize_len << '\n';
		return "Unconstrained: Expansion offset exceeds limit";
	}

	assert (sat == true);
	if (resize_len == res.first->size) {
		std::cerr << "RESIZE_LEN = " << res.first->size << '\n';
		std::cerr << "OFFEST = " << sym_off << '\n';
		return "Unconstrained: Offset already fit in buffer";
	}

	/* expand the real array, copy everything over */
	expandMO(state, resize_len, res);
	return NULL;
}

void UCMMU::expandMO(
	ExecutionState& state, unsigned resize_len, ObjectPair &res)
{
	ObjectState	*new_os;
	MemoryObject	*new_mo;
	ObjectPair	ret;

	new_mo = exe_uc.memory->allocate(
		resize_len, false, true,
		state.getCurrentKFunc()->function, &state);
	assert (res.first != NULL);

	new_mo->setName("uc_buf");
	new_os = exe_uc.executeMakeSymbolic(state, new_mo, "uc_buf");

	/* alias initial contents from smaller buffer into larger buffer */
	/* is this the best way to do this? I'm not sure. */
	for (unsigned i = 0; i < res.first->size; i++) {
		ref<Expr>	cond;

		cond = EqExpr::create(
			state.readSymbolic(res.second, i, 8),
			state.readSymbolic(new_os, i, 8));
		exe_uc.addConstraint(state, cond);
	}

	/* replace first half with updated values from old buffer */
	state.copy(new_os, res.second, res.first->size);

	/* destroy old array */
	state.unbindObject(res.first);

	/* update working object pair; errors to return */
	ret.first = new_mo;
	ret.second = new_os;
	res = ret;
}

void UCMMU::bindUnfixedUC(
	ExecutionState& state,
	MemOp& mop,
	ref<Expr> full_ptr_sym,
	uint64_t residue)
{
	/*
	 * Unfixed buffers on the other hand, fork *and* expand!
	 * Forked states:
	 * + fixed that satisfies new length requirement (expand to fixed),
	 * + symbolic that satisfies len req (expand to sym)
	 */
	unsigned		op_bytes;
	uint64_t		real_addr;
	ObjectPair		res;
	const char		*expand_err;

	std::cerr << "===============WOO: UNFIXED UC================\n";

	op_bytes = Expr::getMinBytesForWidth(mop.getType(exe_uc.getKModule()));
	real_addr = residue;
	if (state.addressSpace.resolveOne(real_addr, res) == false) {
		/* realptr now no where? but I put it there fair and square! */
		exe_uc.terminateOnError(
			state,
			"Unconstrained: bogus realptr. I screwed up! FIXME",
			"uc.err",
			exe_uc.getAddressInfo(state, mop.address));
		return;
	}

	std::cerr << "GOT MO ADDR=" << (void*)res.first->address << '\n';
	std::cerr << "NAME: " << res.first->name << '\n';
	/* not a UC buf. fork and error, don't do any buffer tricks */
	if (res.first->name.substr(0,3) != "uc_") {
		ExeUC::StatePair	sp;
		ref<Expr>		cond;

		std::cerr << "FULLL PTR SYM: " << full_ptr_sym << '\n';
		cond = 	UltExpr::create(
			SubExpr::create(
				full_ptr_sym,
				ConstantExpr::create(
					res.first->address+op_bytes,
					full_ptr_sym->getWidth())),
			ConstantExpr::create(
				res.first->size,
				full_ptr_sym->getWidth())),

		std::cerr << "FORK COND: " << cond << '\n';
		sp = exe_uc.fork(state, cond, true);
		if (sp.second != NULL) {
			exe_uc.terminateOnError(
				*sp.second,
				"bindUnfixedUC: out of bounds on image ptr",
				"fixptr.err",
				exe_uc.getAddressInfo(*sp.second, mop.address));
		}

		if (sp.first == NULL)
			return;

		KleeMMU::memOpError(*sp.first, mop);
		return;
	}

	assert (0 == 1 && "WORK HARDER");
	expand_err = expandRealPtr(
		state,
		real_addr+(op_bytes-1),	// want to expand up to last byte
		full_ptr_sym,
		res);
	if (expand_err != NULL) {
		exe_uc.terminateOnError(
			state,
			expand_err,
			"expand.err",
			exe_uc.getAddressInfo(state, mop.address));
		return;
	}

	assert (0 == 1 && "OOOOPS");
#if 0
	ExeUC::UCPtrFork ucp_fork(
		exe_uc.forkUCPtr(
			state,
			const_cast<MemoryObject*>(res.first),
			exe_uc.sym2idx(sym_resteered)));

	std::cerr << "UC FORKED!!!\n";

	/* abort and retry operation with resized buffers */
	aborted = true;
	new_es = ucp_fork.getState(true);
	assert (new_es != NULL);

	// no need to set offset constraints:
	// they will be added through the fixed buffer error path
	--new_es->prevPC;
	--new_es->pc;

	new_es = ucp_fork.getState(false);
	assert (new_es != NULL);
	--new_es->prevPC;
	--new_es->pc;
#endif
}

void UCMMU::expandUnfixed(
	ExecutionState& state,
	MemOp& mop,
	unsigned pt_idx,
	uint64_t real_addr,
	uint64_t req_addr)
{
	Expr::Width	type;
	unsigned	bytes;
	ExecutionState	*new_es;
	ObjectPair	res;
	unsigned	min_off, round_off;

	type = mop.getType(exe_uc.getKModule());
	bytes = Expr::getMinBytesForWidth(type);

	if (state.addressSpace.resolveOne(real_addr, res) == false) {
		exe_uc.terminateOnError(
			state,
			"Unconstrained: bogus realptr. I screwed up! FIXME",
			"uc.err",
			exe_uc.getAddressInfo(state, mop.address));
		return;
	}

	/* get new minimum offset */
	min_off = (req_addr - real_addr) + bytes;
	if (min_off > MAX_EXPAND_SIZE) {
		std::cerr << "MAXBUF: " << min_off << '\n';
		exe_uc.terminateOnError(
			state,
			"Unconstrained: Maximum buffer size reached.",
			"ucmax.err",
			exe_uc.getAddressInfo(state, mop.address));
		return;
	}

	round_off = res.first->size;
	while (round_off < min_off)
		round_off *= 2;

	expandMO(state, round_off, res);

	/* finally, fork expand-- create one fixed and one symbolic */
	ExeUC::UCPtrFork ucp_fork(
		exe_uc.forkUCPtr(
			state,
			const_cast<MemoryObject*>(res.first),
			pt_idx));

	/* abort and retry operation with resized buffers */
	aborted = true;
	new_es = ucp_fork.getState(true);
	assert (new_es != NULL);

	// no need to set offset constraints:
	// they will be added through the fixed buffer error path
	new_es->abortInstruction();

	new_es = ucp_fork.getState(false);
	assert (new_es != NULL);
	new_es->abortInstruction();
}


void UCMMU::bindFixedUC(
	ExecutionState& unbound, MemOp& mop,
	uint64_t real_addr)
{
	Expr::Width	type;
	unsigned	bytes;
	ExecutionState	*oob_state;
	ObjectPair	res;

	type = mop.getType(exe_uc.getKModule());
	bytes = Expr::getMinBytesForWidth(type);

	if (unbound.addressSpace.resolveOne(real_addr, res) == false) {
		/* realptr now no where? but I put it there fair and square! */
		exe_uc.terminateOnError(
			unbound,
			"Unconstrained: bogus realptr. I screwed up! FIXME",
			"uc.err",
			exe_uc.getAddressInfo(unbound, mop.address));
		return;
	}

	/* Unlike regular MMU,
	 * don't bother trying to spawn off forks for all addresses--
	 * We just care about binding the variable for the small buffer and
	 * generating a terminating test-case.
	 * (Alternative: fork to all UC base pointers)
	 *
	 * Separate case: unfixed uc */
	std::cerr << "ADDR: " << mop.address << '\n';
	oob_state = getUnboundAddressState(&unbound, mop, res, bytes, type);

	/* two states forked from unbounded:
	 * + bound state is now committed (from getUAS)
	 * + unbound state that is not committed (if exists; terminate) */
	if (oob_state == NULL)
		return;

	exe_uc.terminateOnError(
		*oob_state,
		"memory error: out of bound pointer",
		"ptr.err",
		exe_uc.getAddressInfo(*oob_state, mop.address));
}

void UCMMU::assignNewPointer(
	ExecutionState& state, MemOp& mop, uint64_t residue)
{
	int	ptab_idx;

	std::cerr << "ASSIGNING NEW POINTER!\n";
	assert (resteer.ptrtab_idxs.size() == 1);
	if (resteer.untracked.size() != 0) {
		std::cerr << "ANTHONY: IMPLEMENT SCHEDULING CHOICE HERE\n";
	}

	ptab_idx = (resteer.ptrtab_idxs.begin())->first;
	std::cerr << "IDX = " << ptab_idx << '\n';

	if (exe_uc.isRegIdx(ptab_idx)) {
		assignRegPointer(state, mop, residue, ptab_idx);
		return;
	}

	assert (0 == 1 && "DEEP RESOLUTION. WHOOPS");
}

void UCMMU::assignRegPointer(
	ExecutionState& state, MemOp& mop, uint64_t residue,
	int ptab_idx)
{
	/* finish up by doing memop resolve on still-running state
	 * for forked state, back the PC up and restart execution. */
	ExecutionState	*new_es;
	ObjectState	*reg_wos;
	unsigned	reg_off, min_sz;
	ref<Expr>	reg_sym, sym_ptr;

	/* create the initial fork, first=len<=resi second len>resi
	 * if resi < 8, resi = 8*/
	std::cerr << "assignNewPointer: RESIDUE: " << (void*)residue << '\n';

	min_sz = residue+mop.getType(exe_uc.getKModule());
	reg_off = ptab_idx * exe_uc.getPtrBytes();

	reg_sym = state.readSymbolic(
		GETREGOBJRO(state), reg_off, exe_uc.getPtrBytes()*8);

	ExeUC::UCPtrFork ucp_fork(
		exe_uc.initUCPtr(state, ptab_idx, reg_sym, min_sz));

	new_es = ucp_fork.getState(true);
	new_es->abortInstruction();

	/* since this is a root pointer, it'll stay in the register list--
	 * set its value to the ptr table */

	/* write back to register */
	reg_wos = GETREGOBJ(*new_es);
	sym_ptr = exe_uc.getUCSymPtr(*new_es, ptab_idx);
	reg_sym = new_es->readSymbolic(reg_wos, reg_off, sym_ptr->getWidth());
	exe_uc.addConstraint(*new_es, EqExpr::create(reg_sym, sym_ptr));
	reg_wos->writeIVC(reg_off, dyn_cast<ConstantExpr>(sym_ptr));


	new_es = ucp_fork.getState(false);
	new_es->abortInstruction();

	reg_wos = GETREGOBJ(*new_es);
	sym_ptr = exe_uc.getUCSymPtr(*new_es, ptab_idx);
	reg_sym = new_es->readSymbolic(reg_wos, reg_off, sym_ptr->getWidth());
	exe_uc.addConstraint(*new_es, EqExpr::create(reg_sym, sym_ptr));
	new_es->write(reg_wos, reg_off, sym_ptr);

	aborted = true;

	std::cerr << "BACK IT UP HONEY\n";
}

void UCMMU::resolveSymbolicOffset(
	ExecutionState& state, MemOp& mop, uint64_t residue)
{
	ref<Expr>	sym_ptr;
	int		pt_idx;

	pt_idx = -1;
	if (resteer.untracked.size() != 0) {
		std::cerr << "FIRST UNTRACKED: ";
		std::cerr << *(resteer.untracked.begin()) << '\n';
	}

	if (resteer.ptrtab_idxs.size() == 0) {
		/* No pointers are rewritten here--
		 * that means we're certainly dealing with fixed buffers. */
		bindFixedUC(state, mop, residue);
		return;
	}

	assert (resteer.ptrtab_idxs.size() == 1);
	pt_idx = resteer.ptrtab_idxs.begin()->first;

	/* Use pt_idx to find out whether realptr is bound to
	 * the symptr (symptr=const => bound) */
	sym_ptr = exe_uc.getUCSymPtr(state, pt_idx);
	if (isa<ConstantExpr>(sym_ptr)) {
		bindFixedUC(state, mop, residue);
		return;
	}

	std::cerr << "PTIDX: " << pt_idx << '\n';
	std::cerr << "RESIDUE: " << (void*)residue << '\n';
	std::cerr << "SYMPTR: " << sym_ptr << '\n';
	std::cerr << "NEW PTR: " << resteer.new_expr << "\n";
	std::cerr << "ORIGINAL PTR: " << resteer.old_expr << "\n";

	bindUnfixedUC(state, mop, resteer.new_expr, residue);

	std::cerr << "BIND UNFIXED DONE.\n";
}

void UCMMU::handleSymResteer(ExecutionState& state, MemOp& mop)
{
	/* two possibilities:
	 * 1. rewrite syms=0 => small constant => a sym is a pointer
	 * 2. rewrite syms=0 => large constant => the sym is an offset
	 */
	Assignment		a(resteer.new_expr);
	ref<Expr>		zero_syms;
	uint64_t		residue;
	const ConstantExpr	*ce;

	a.bindFreeToZero();
	zero_syms = a.evaluate(resteer.new_expr);
	ce = dyn_cast<ConstantExpr>(zero_syms);
	assert (ce != NULL);

	residue = ce->getZExtValue();
	if (residue >= MAX_EXPAND_SIZE) {
		/* large constant => sym is an offset */
		if ((int64_t)residue < 0) {
			klee_warning(
				"Ignoring negative residue %p\n",
				(void*)residue);
			exe_uc.terminateOnExit(state);
			return;
		}
		resolveSymbolicOffset(state, mop, residue);
	} else {
		/* small constant => sym is a pointer */
		assignNewPointer(state, mop, residue);
	}
}

void UCMMU::memOpError(ExecutionState& state, MemOp& mop)
{
	const ConstantExpr	*new_ce, *sym_ce;

	/* aborted? then we're rolling back.. do nothing */
	if (aborted)
		return;

	if (resteered == false)
		return KleeMMU::memOpError(state, mop);

	new_ce = dyn_cast<ConstantExpr>(resteer.new_expr);
	if (new_ce == NULL) {
		handleSymResteer(state, mop);
		return;
	}

	/* a resteer.new_expr with a constant value ending up here
	 * implies that we had some kind of buffer overrun */

	assert (resteer.ptrtab_idxs.size() == 1);
	int pt_idx = resteer.ptrtab_idxs.begin()->first;

	ref<Expr> sym_ptr = exe_uc.getUCSymPtr(state, pt_idx);
	sym_ce = dyn_cast<ConstantExpr>(sym_ptr);

	std::cerr << "OLD EXPR: " << resteer.old_expr << '\n';
	std::cerr << "NEW EXPR: " << resteer.new_expr << '\n';
	std::cerr << "SYM PTR: " << sym_ptr << '\n';

	if (sym_ce != NULL) {
		/* dead on a fixed buffer.. not much we can do */
		exe_uc.terminateOnError(
			state,
			"Unconstrained: exceeded length of fixed buffer.",
			"ptr.err",
			exe_uc.getAddressInfo(state, mop.address));
		return;
	}

	std::cerr << "UNFIXED BIND: RESTEERING: ADDR: ";

	expandUnfixed(
		state,
		mop,
		pt_idx,
		exe_uc.getUCSym2Real(state, sym_ptr),
		new_ce->getZExtValue());
}

bool UCMMU::exeMemOp(ExecutionState &state, MemOp& mop)
{
	is_write = mop.isWrite;
	aborted = false;
	resteered = false;
	return KleeMMU::exeMemOp(state, mop);
}
