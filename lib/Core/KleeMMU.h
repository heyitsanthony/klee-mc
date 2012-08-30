#ifndef KLEEMMU_H
#define KLEEMMU_H

#include "Memory.h"
#include "TLB.h"
#include "MMU.h"

namespace klee
{

class Executor;
class ExecutionState;
class KModule;
class KInstruction;

class KleeMMU : public MMU
{
public:
	KleeMMU(Executor& e) : MMU(e) {}
	virtual ~KleeMMU() {}

	// do address resolution / object binding / out of bounds checking
	// and perform the operation
	virtual bool exeMemOp(ExecutionState &state, MemOp& mop);

protected:
	struct MemOpRes
	{
	public:
		static MemOpRes failure()
		{
			MemOpRes	r;
			r.usable = false;
			r.rc = false;
			return r;
		}

		bool isBad(void) const { return !rc || !usable; }

		ObjectPair		op;
		ref<Expr>		offset;
		const MemoryObject	*mo;
		const ObjectState	*os;
		bool			usable;
		bool			rc;	/* false => solver failure */
	};

	virtual MemOpRes memOpResolve(
		ExecutionState& state,
		ref<Expr> address,
		Expr::Width type);

	KleeMMU::MemOpRes memOpResolveConst(
		ExecutionState& state, uint64_t addr, Expr::Width type);

	KleeMMU::MemOpRes memOpResolveExpr(
		ExecutionState& state, ref<Expr> addr, Expr::Width type);

	bool memOpFast(ExecutionState& state, MemOp& mop);

	void writeToMemRes(
		ExecutionState& state,
		const struct MemOpRes& res,
		const ref<Expr>& value);

	bool memOpByByte(ExecutionState& state, MemOp& mop);

	ExecutionState* getUnboundAddressState(
		ExecutionState	*unbound,
		MemOp&		mop,
		ObjectPair	&resolution,
		unsigned	bytes,
		Expr::Width	type);

	virtual void memOpError(ExecutionState& state, MemOp& mop);

	// Called on [for now] concrete reads, replaces constant with a symbolic
	// Used for testing.
	ref<Expr> replaceReadWithSymbolic(ExecutionState &state, ref<Expr> e);


	void commitMOP(
		ExecutionState& state,
		const MemOp& mop,
		const MemOpRes& res);
private:
	TLB			tlb;
};

}

#endif
