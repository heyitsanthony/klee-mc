#define GUEST_ARCH_AMD64
#include "../syscall/syscalls.h"
#include "klee/klee.h"

static unsigned sys_c = 0;
static unsigned max_sys_c = 0;

void* __hookpost_sc_enter(void* ret_v)
{
	if (!max_sys_c) {
		max_sys_c = klee_read_reg("syscalls_max");
	}

	sys_c++;
	if (sys_c > max_sys_c) {
		klee_print_expr("[sysexit] Exiting early on nth syscall", sys_c);
		kmc_exit((uint64_t)ret_v);
	}

	return ret_v;
}