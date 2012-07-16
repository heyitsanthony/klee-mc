#include "klee/ExecutionState.h"
#include "Executor.h"
#include "ConcreteMMU.h"

using namespace klee;

bool ConcreteMMU::exeMemOp(ExecutionState &state, MemOp& mop)
{
	const ConstantExpr	*ce;

	ce = dyn_cast<ConstantExpr>(mop.address);
	if (ce == NULL) {
		/* ignore non-const addresses */
		return false;
	}

	exeConstMemOp(state, mop, ce->getZExtValue());
	return true;
}

void ConcreteMMU::exeConstMemOp(
	ExecutionState	&state,
	MemOp		&mop,
	uint64_t	addr)
{
	Expr::Width	type;
	unsigned	byte_c;
	ObjectPair	op;

	type = mop.getType(exe.getKModule());
	if (lookup(state, addr, type, op)) {
		/* fast, full-width path */
		commitMOP(state, mop, op, addr);
		return;
	}

	byte_c = Expr::getMinBytesForWidth(type);
	if (byte_c != 1) {
		if (mop.isWrite) {
			if (slowPathWrite(state, mop, addr))
				return;
		} else {
			if (slowPathRead(state, mop, addr))
				return;
		}
	}

	/*  BAD ACCESS */
	exe.terminateStateOnError(
		state,
		"memory error: out of bound pointer",
		"ptr.err",
		exe.getAddressInfo(state, mop.address));
}

bool ConcreteMMU::slowPathRead(
	ExecutionState	&state,
	MemOp		&mop,
	uint64_t	addr)
{
	Expr::Width	type;
	unsigned	byte_c;
	ref<Expr>	r_val;

	type = mop.getType(exe.getKModule());
	byte_c = Expr::getMinBytesForWidth(type);
	for (unsigned i = 0; i < byte_c; i++, addr++) {
		ObjectPair	op;
		uint64_t	off;

		if (lookup(state, addr, 8, op) == false)
			return false;

		off = op.first->getOffset(addr);
		ref<Expr> r(state.read(op.second, off, 8));
		if (r_val.isNull()) r_val = r;
		else r_val = MK_CONCAT(r, r_val);
	}

	state.bindLocal(mop.target, r_val);
	return true;
}

bool ConcreteMMU::slowPathWrite(
	ExecutionState	&state,
	MemOp		&mop,
	uint64_t	addr)
{
	Expr::Width	type;
	unsigned	byte_c;

	type = mop.getType(exe.getKModule());
	byte_c = Expr::getMinBytesForWidth(type);
	for (unsigned i = 0; i < byte_c; i++, addr++) {
		ObjectState 	*wos;
		ObjectPair	op;
		uint64_t	off;

		if (lookup(state, addr, 8, op) == false)
			return false;

		off = op.first->getOffset(addr);

		ref<Expr>	byte_val(MK_EXTRACT(mop.value, 8*i, 8));

		if (op.second->readOnly) {
			exe.terminateStateOnError(
				state,
				"memory error: object read only",
				"readonly.err");
			return false;
		}

		wos = state.addressSpace.getWriteable(op);
		state.write(wos, off, byte_val);
	}
	return true;
}


void ConcreteMMU::commitMOP(
	ExecutionState	&state,
	MemOp		&mop,
	ObjectPair	&op,
	uint64_t	addr)
{
	ObjectState 	*wos;
	uint64_t	off;
	Expr::Width	type;

	off = op.first->getOffset(addr);
	type = mop.getType(exe.getKModule());

	if (mop.isWrite == false) {
		ref<Expr> r(state.read(op.second, off, type));
		state.bindLocal(mop.target, r);
		return;
	}
	
	if (op.second->readOnly) {
		exe.terminateStateOnError(
			state,
			"memory error: object read only",
			"readonly.err");
		return;
	}

	wos = state.addressSpace.getWriteable(op);
	state.write(wos, off, mop.value);
}

bool ConcreteMMU::lookup(
	ExecutionState& state,
	uint64_t addr,
	unsigned type,
	ObjectPair& op)
{
	unsigned	bytes;
	bool		tlb_hit, in_bounds;

	bytes = Expr::getMinBytesForWidth(type);

	tlb_hit = tlb.get(state, addr, op);
	if (tlb_hit && op.first->isInBounds(addr, bytes))
		return true;

	if (state.addressSpace.resolveOne(addr, op) == false) {
		/* no feasible objects */
		return false;
	}
	tlb.put(state, op);

	in_bounds = op.first->isInBounds(addr, bytes);
	if (in_bounds == false) {
		/* not in bounds */
		return false;
	}

	return true;
}
