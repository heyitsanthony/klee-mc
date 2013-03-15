#ifndef CONC_MMU_H
#define CONC_MMU_H

#include "MMU.h"
#include "TLB.h"

namespace klee
{
class ConcreteMMU : public MMU
{
public:
	ConcreteMMU(Executor& exe) : MMU(exe) {}
	virtual ~ConcreteMMU(void) {}

	// do address resolution / object binding / out of bounds checking
	// and perform the operation
	virtual bool exeMemOp(ExecutionState &state, MemOp& mop);

	void commitMOP(
		ExecutionState	&state,
		MemOp		&mop,
		ObjectPair	&op,
		uint64_t	addr);

	bool lookup(
		ExecutionState& state,
		uint64_t addr,
		unsigned type,
		ObjectPair& op);

private:
	bool slowPathRead(ExecutionState &state, MemOp &mop, uint64_t addr);
	bool slowPathWrite(ExecutionState &state, MemOp &mop, uint64_t addr);
	void exeConstMemOp(
		ExecutionState	&state,
		MemOp		&mop,
		uint64_t	addr);

	TLB	tlb;
};
}
#endif
