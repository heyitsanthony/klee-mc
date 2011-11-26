#include <llvm/Support/CommandLine.h>
#include <llvm/Target/TargetData.h>

#include "static/Sugar.h"
#include "TimingSolver.h"
#include "Executor.h"
#include "MMU.h"
#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"

using namespace llvm;

extern unsigned MakeConcreteSymbolic;

namespace {
	cl::opt<bool>
	SimplifySymIndices(
		"simplify-sym-indices",
		cl::desc("Simplify indicies/values on mem access."),
		cl::init(false));

	cl::opt<unsigned>
	MaxSymArraySize(
		"max-sym-array-size",
		cl::desc("Concretize accesses to large symbolic arrays"),
		cl::init(0));
}

using namespace klee;

void MMU::writeToMemRes(
  	ExecutionState& state,
	struct MemOpRes& res,
	ref<Expr> value)
{
	if (res.os->readOnly) {
		exe.terminateStateOnError(
			state,
			"memory error: object read only",
			"readonly.err");
	} else {
		ObjectState *wos;
		wos = state.addressSpace.getWriteable(res.mo, res.os);
		state.write(wos, res.offset, value);
	}
}

Expr::Width MMU::MemOp::getType(const KModule* m) const
{
	return (isWrite
		? value->getWidth()
		: m->targetData->getTypeSizeInBits(target->inst->getType()));
}

void MMU::MemOp::simplify(ExecutionState& state)
{
	if (!isa<ConstantExpr>(address))
		address = state.constraints.simplifyExpr(address);
	if (isWrite && !isa<ConstantExpr>(value))
		value = state.constraints.simplifyExpr(value);
}

/* handles a memop that can be immediately resolved */
bool MMU::memOpFast(ExecutionState& state, MemOp& mop)
{
	Expr::Width	type;
	MemOpRes	res;

	type = mop.getType(exe.getKModule());
	res = memOpResolve(state, mop.address, type);
	if (!res.usable || !res.rc)
		return false;

	if (mop.isWrite) {
		writeToMemRes(state, res, mop.value);
	} else {
		ref<Expr> result = state.read(res.os, res.offset, type);
		if (MakeConcreteSymbolic)
			result = replaceReadWithSymbolic(state, result);

		state.bindLocal(mop.target, result);
	}

	return true;
}

ref<Expr> MMU::readDebug(ExecutionState& state, uint64_t addr)
{
	ObjectPair	op;
	uint64_t	off;
	bool		found;

	found = state.addressSpace.resolveOne(addr, op);
	if (found == false)
		return NULL;

	off = addr - op.first->address;
	return state.read(op.second, off, 64);
}

ExecutionState* MMU::getUnboundAddressState(
	ExecutionState	*unbound,
	MemOp		&mop,
	ObjectPair	&resolution,
	unsigned	bytes,
	Expr::Width	type)
{
	MemOpRes	res;
	ExecutionState	*bound;
	ref<Expr>	inBoundPtr;

	res.op = resolution;
	res.mo = res.op.first;
	res.os = res.op.second;
	inBoundPtr = res.mo->getBoundsCheckPointer(mop.address, bytes);

	Executor::StatePair branches(exe.fork(*unbound, inBoundPtr, true));
	bound = branches.first;

	// bound can be 0 on failure or overlapped
	if (bound == NULL) {
		return branches.second;
	}

	res.offset = res.mo->getOffsetExpr(mop.address);
	if (mop.isWrite) {
		writeToMemRes(*bound, res, mop.value);
	} else {
		ref<Expr> result;
		result = bound->read(res.os, res.offset, type);
		bound->bindLocal(mop.target, result);
	}

	return branches.second;
}

void MMU::memOpError(ExecutionState& state, MemOp& mop)
{
	Expr::Width	type;
	unsigned	bytes;
	ResolutionList	rl;
	ExecutionState	*unbound;
	bool		incomplete;

	type = mop.getType(exe.getKModule());
	bytes = Expr::getMinBytesForWidth(type);

	incomplete = state.addressSpace.resolve(
		state, exe.getSolver(), mop.address, rl, 0);

	// XXX there is some query wasteage here. who cares?
	unbound = &state;
	foreach (it, rl.begin(), rl.end()) {
		ObjectPair	res(*it);

		unbound = getUnboundAddressState(
			unbound, mop, res, bytes, type);

		/* bad unbound state.. terminate */
		if (!unbound)
			return;
	}

	// XXX should we distinguish out of bounds and overlapped cases?
	if (incomplete) {
		exe.terminateStateEarly(*unbound, "query timed out (resolve)");
		return;
	}

	exe.terminateStateOnError(
		*unbound,
		"memory error: out of bound pointer",
		"ptr.err",
		exe.getAddressInfo(*unbound, mop.address));
}

void MMU::exeMemOp(ExecutionState &state, MemOp mop)
{
	if (SimplifySymIndices) mop.simplify(state);

	if (memOpFast(state, mop))
		return;

	// handle straddled accesses
	if (memOpByByte(state, mop))
		return;

	// we are on an error path
	// Possible reasons:
	// 	* no resolution
	// 	* multiple resolution
	// 	* one resolution with out of bounds
	memOpError(state, mop);
}

ref<Expr> MMU::replaceReadWithSymbolic(
	ExecutionState &state, ref<Expr> e)
{
	static unsigned	id;
	const Array	*array;
	unsigned	n;

	n = MakeConcreteSymbolic;
	if (!n || exe.isReplayOut() || exe.isReplayPaths()) return e;

	// right now, we don't replace symbolics (is there any reason too?)
	if (!isa<ConstantExpr>(e)) return e;

	/* XXX why random? */
	if (n != 1 && random() % n) return e;

	// create a new fresh location, assert it is equal to
	// concrete value in e and return it.

	array = new Array(
		"rrws_arr" + llvm::utostr(++id),
		MallocKey(Expr::getMinBytesForWidth(e->getWidth())));

	ref<Expr> res = Expr::createTempRead(array, e->getWidth());
	ref<Expr> eq = NotOptimizedExpr::create(EqExpr::create(e, res));
	std::cerr << "Making symbolic: " << eq << "\n";
	state.addConstraint(eq);
	return res;
}

/* handles a memop that can be immediately resolved */
bool MMU::memOpByByte(ExecutionState& state, MemOp& mop)
{
	Expr::Width	type;
	unsigned	bytes;
	ref<Expr>	read_expr;

	type = mop.getType(exe.getKModule());
	if ((type % 8) != 0) return false;

	bytes = Expr::getMinBytesForWidth(type);
	for (unsigned i = 0; i < bytes; i++) {
		ref<Expr>	byte_addr;
		ref<Expr>	byte_val;
		MemOpRes	res;

		byte_addr = AddExpr::create(
			mop.address,
			ConstantExpr::create(i, mop.address->getWidth()));

		res = memOpResolve(state, byte_addr, 8);
		if (!res.usable || !res.rc)
			return false;
		if (mop.isWrite) {
			byte_val = ExtractExpr::create(mop.value, 8*i, 8);
			writeToMemRes(state, res, byte_val);
		} else {
			ref<Expr> result = state.read(res.os, res.offset, 8);
			if (read_expr.isNull())
				read_expr = result;
			else
				read_expr = ConcatExpr::create(
					result, read_expr);
		}
	}

	// we delayed setting the local variable on the read because
	// we need to respect the original type width-- result is concatenated
	if (!mop.isWrite) {
		state.bindLocal(mop.target, read_expr);
	}

	return true;
}

MMU::MemOpRes MMU::memOpResolve(
	ExecutionState& state,
	ref<Expr> addr,
	Expr::Width type)
{
	MemOpRes	ret;
	unsigned	bytes;
	bool		alwaysInBounds;

	bytes = Expr::getMinBytesForWidth(type);
	ret.usable = false;

	ret.rc = state.addressSpace.getFeasibleObject(
		state, exe.getSolver(), addr, ret.op);
	if (!ret.rc) {
		/* solver failed in GFO, force addr to be concrete */
		addr = exe.toConstant(state, addr, "resolveOne failure");
		ret.op.first = NULL;
		ret.rc = state.addressSpace.resolveOne(
			cast<ConstantExpr>(addr), ret.op);
		if (!ret.rc)
			return ret;
	}

	if (ret.op.first == NULL) {
		/* no feasible objects exist */
		return ret;
	}

	// fast path: single in-bounds resolution.
	assert (ret.op.first != NULL);
	ret.mo = ret.op.first;
	ret.os = ret.op.second;

	if (MaxSymArraySize && ret.mo->size >= MaxSymArraySize) {
		addr = exe.toConstant(state, addr, "max-sym-array-size");
	}

	ret.offset = ret.mo->getOffsetExpr(addr);

	/* verify access is in bounds */
	ret.rc = exe.getSolver()->mustBeTrue(
		state,
		ret.mo->getBoundsCheckOffset(ret.offset, bytes),
		alwaysInBounds);
	if (!ret.rc) {
		state.pc = state.prevPC;
		exe.terminateStateEarly(state, "query timed out");
		ret.rc = false;
		return ret;
	}

	if (!alwaysInBounds)
		return ret;

	ret.usable = true;
	return ret;
}