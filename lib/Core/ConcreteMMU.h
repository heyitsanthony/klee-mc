#ifndef CONC_MMU_H
#define CONC_MMU_H

#include "MMU.h"

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
private:
};
}
#endif
