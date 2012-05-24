#ifndef MMU_H
#define MMU_H

#include "Memory.h"
#include "TLB.h"

namespace klee
{

class Executor;
class ExecutionState;
class KModule;
class KInstruction;

class MMU
{
public:
	MMU(Executor& e) : exe(e) {}
	virtual ~MMU() {}

	struct MemOp
	{
	public:
		Expr::Width getType(const KModule* m) const;
		void simplify(ExecutionState& es);
		MemOp(	bool _isWrite, ref<Expr> _addr, ref<Expr> _value,
			KInstruction* _target)
		: type_cache(-1)
		, isWrite(_isWrite)
		, address(_addr)
		, value(_value)
		, target(_target)
		{}

		mutable int	type_cache;
		bool		isWrite;
		ref<Expr>	address;
		ref<Expr>	value;		/* undef if read */
		KInstruction	*target;	/* undef if write */
	};

	// do address resolution / object binding / out of bounds checking
	// and perform the operation
	virtual void exeMemOp(ExecutionState &state, MemOp mop);

	/* small convenience function that should ONLY be used for debugging */
	ref<Expr> readDebug(ExecutionState& state, uint64_t addr);

	static uint64_t getQueries(void) { return query_c; }

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

	MMU::MemOpRes memOpResolveConst(
		ExecutionState& state, uint64_t addr, Expr::Width type);

	MMU::MemOpRes memOpResolveExpr(
		ExecutionState& state, ref<Expr> addr, Expr::Width type);

	bool memOpFast(ExecutionState& state, MemOp& mop);

	void writeToMemRes(
		ExecutionState& state,
		const struct MemOpRes& res,
		ref<Expr> value);

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
	Executor		&exe;
	static uint64_t		query_c;
	TLB			tlb;
};

}

#endif
