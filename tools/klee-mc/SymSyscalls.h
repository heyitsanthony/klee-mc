#ifndef KLEEMC_SYMSYSCALLS_H
#define KLEEMC_SYMSYSCALLS_H

#include <stdint.h>

class SyscallParams;

namespace klee {

class ExecutionState;
class ObjectState;
class ExecutorVex;
class KInstruction;

class SymSyscalls
{
public:
	SymSyscalls(ExecutorVex* owner);
	virtual ~SymSyscalls(void) {}
	bool apply(
		ExecutionState& st, KInstruction *ki, const SyscallParams& sp);

	ObjectState* sc_ret_ge0(ExecutionState& state);
	ObjectState* sc_ret_le0(ExecutionState& state);
	ObjectState* sc_ret_or(ExecutionState& state, uint64_t o1, uint64_t o2);
	ObjectState* sc_ret_range(
		ExecutionState& state, uint64_t lo, uint64_t hi);
	void sc_ret_v(ExecutionState& state, uint64_t v);
private:
	void sc_fail(ExecutionState& state);

#define DECL_SC(x)	\
	void sc_##x(ExecutionState& state, const SyscallParams &sp);

	void sc_writev(ExecutionState& state);
	DECL_SC(getcwd)
	DECL_SC(munmap)
	DECL_SC(stat)
	DECL_SC(read)
	DECL_SC(getdents)
	void sc_mmap(
		ExecutionState& state, 
		const SyscallParams &sp, KInstruction* ki);
	
	ObjectState* makeSCRegsSymbolic(ExecutionState& state);

	ExecutorVex	*exe_vex;
	unsigned int	sc_dispatched;
	unsigned int	sc_retired;
	unsigned int	syscall_c[512];
};

}
#endif
