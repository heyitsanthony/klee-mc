#include "klee/ExecutionState.h"
#include "Executor.h"
#include "ConcreteMMU.h"

using namespace klee;

#define MOP_BYTES	Expr::getMinBytesForWidth(mop.getType(exe.getKModule()))

bool ConcreteMMU::exeMemOp(ExecutionState &state, MemOp& mop)
{
	if (mop.address->getKind() != Expr::Constant) {
		/* ignore non-const addresses */
		return false;
	}

	auto ce = cast<ConstantExpr>(mop.address);
	exeConstMemOp(state, mop, ce->getZExtValue());
	return true;
}

void ConcreteMMU::exeConstMemOp(
	ExecutionState	&state,
	MemOp		&mop,
	uint64_t	addr)
{
	ObjectPair	op;
	unsigned	byte_c = MOP_BYTES;

	if (lookup(state, addr, byte_c, op)) {
		/* fast, full-width path */
		commitMOP(state, mop, op, addr);
		return;
	}

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
	TERMINATE_ERROR_LONG(&exe, state,
		"memory error: out of bound pointer",
		"ptr.err",
		exe.getAddressInfo(state, mop.address),
		false);
}

bool ConcreteMMU::slowPathRead(
	ExecutionState	&state,
	MemOp		&mop,
	uint64_t	addr)
{
	unsigned	byte_c = MOP_BYTES;
	ref<Expr>	r_val;

	for (unsigned i = 0; i < byte_c; i++, addr++) {
		ObjectPair	op;

		if (lookup(state, addr, 1, op) == false)
			return false;

		auto r(state.read(op.second, op.first->getOffset(addr), 8));
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
	unsigned	byte_c = MOP_BYTES;

	for (unsigned i = 0; i < byte_c; i++, addr++) {
		ObjectPair	op;

		if (lookup(state, addr, 1, op) == false)
			return false;

		if (op.second->readOnly) {
			TERMINATE_ERROR(&exe, state,
				"memory error: object read only",
				"readonly.err");
			return false;
		}

		state.write(	state.addressSpace.getWriteable(op),
				op.first->getOffset(addr),
				MK_EXTRACT(mop.value, 8 * i, 8));
	}

	return true;
}


void ConcreteMMU::commitMOP(
	ExecutionState	&state,
	MemOp		&mop,
	ObjectPair	&op,
	uint64_t	addr)
{
	uint64_t	off = op.first->getOffset(addr);

	if (mop.isWrite == false) {
		state.bindLocal(mop.target,
				state.read(
					op.second,
					off,
					mop.getType(exe.getKModule())));
		return;
	}
	
	if (op.second->readOnly) {
		TERMINATE_ERROR(&exe, state,
			"memory error: object read only",
			"readonly.err");
		return;
	}

	state.write(state.addressSpace.getWriteable(op), off, mop.value);
}

bool ConcreteMMU::lookup(
	ExecutionState& state,
	uint64_t addr,
	unsigned byte_c,
	ObjectPair& op)
{
	bool		tlb_hit, in_bounds;

	tlb_hit = tlb.get(state, addr, op);
	if (tlb_hit && op.first->isInBounds(addr, byte_c))
		return true;

	if (state.addressSpace.resolveOne(addr, op) == false) {
		/* no feasible objects */
		return false;
	}
	tlb.put(state, op);

	in_bounds = op.first->isInBounds(addr, byte_c);
	if (in_bounds == false) {
		/* not in bounds */
		return false;
	}

	return true;
}
