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

	// did the memory layout change?
	virtual void signal(ExecutionState& state,
		void* addr, uint64_t len) {}

	/* small convenience function that should ONLY be used for debugging */
	ref<Expr> readDebug(ExecutionState& state, uint64_t addr);

	static uint64_t getQueries(void) { return query_c; }
	static uint64_t getSymAccesses(void) { return sym_w_c + sym_r_c; }
	static uint64_t getSymWrites(void) { return sym_w_c; }
	static uint64_t getSymReads(void) { return sym_r_c; }

	static MMU* create(Executor& exe);

	Executor& getExe(void) { return exe; }

	static bool isSymMMU(void);
	static bool isSoftConcreteMMU(void);
protected:
	MMU(Executor& e) : exe(e) {}

	Executor		&exe;
	static uint64_t		query_c;
	static uint64_t		sym_w_c;
	static uint64_t		sym_r_c;
};

}

#endif
