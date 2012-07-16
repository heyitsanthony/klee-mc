#ifndef MMU_H
#define MMU_H

#include "Memory.h"

namespace klee
{

class Executor;
class ExecutionState;
class KModule;
class KInstruction;

class MMU
{
public:
	virtual ~MMU() {}

	class MemOp
	{
	public:
		Expr::Width getType(const KModule* m) const;
		void simplify(ExecutionState& es);
		MemOp(	bool _isWrite, ref<Expr> _addr, ref<Expr> _value,
			KInstruction* _target)
		: isWrite(_isWrite)
		, address(_addr)
		, value(_value)
		, target(_target)
		, type_cache(-1)
		{}

		bool		isWrite;
		ref<Expr>	address;
		ref<Expr>	value;		/* undef if read */
		KInstruction	*target;	/* undef if write */

	private:
		mutable int	type_cache;
	};

	// do address resolution / object binding / out of bounds checking
	// and perform the operation
	virtual bool exeMemOp(ExecutionState &state, MemOp& mop) = 0;

	/* small convenience function that should ONLY be used for debugging */
	ref<Expr> readDebug(ExecutionState& state, uint64_t addr);

	static uint64_t getQueries(void) { return query_c; }

	static MMU* create(Executor& exe);
protected:
	MMU(Executor& e) : exe(e) {}

	Executor		&exe;
	static uint64_t		query_c;
};

}

#endif
