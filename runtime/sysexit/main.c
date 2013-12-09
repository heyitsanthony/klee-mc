#define GUEST_ARCH_AMD64
#include "../syscall/syscalls.h"
#include "klee/klee.h"

void* __hookpost_sc_enter(void* ret_v)
{
	kmc_exit((uint64_t)ret_v);
	return ret_v;
}