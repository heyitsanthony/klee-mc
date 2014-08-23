#define GUEST_ARCH_AMD64
#include "klee/klee.h"
#include "../syscall/syscalls.h"

/* assumes this is called in a pre hook */
void kmc_skip_func()
{
	void	*r = kmc_regs_get();
	void	*new_pc;
	
	new_pc = *((uint64_t*)GET_STACK(r));
	new_pc = klee_fork_all(new_pc);
	GET_PC(r) = new_pc;
	GET_STACK(r) = GET_STACK(r) + sizeof(uint64_t); /* pop */
	__kmc_skip_func();
}
