/**
 * implementation of heap-tracking mmu deal.
 * looks for accesses to heap data */
#include "klee/klee.h"
#define GUEST_ARCH_AMD64
#include "../syscall/syscalls.h"
#include "mmu.h"


void post_int_free(int64_t retval)
{ klee_print_expr("exiting free", retval);  }

void post__int_malloc(int64_t retval)
{ klee_print_expr("exiting malloc", retval); }

void __hookpre__int_free(void* regfile)
{
	klee_print_expr("entering free", 0);
	klee_hook_return(1, &post_int_free, GET_ARG0(regfile));
}

void __hookpre__int_malloc(void* regfile)
{
	klee_print_expr("entering malloc", 0);
	klee_hook_return(1, &post__int_malloc, GET_ARG0(regfile));
}

#define MMU_LOADC(x,y)			\
y mmu_load_##x##_memcheckc(void* addr)	\
{ return mmu_load_##x##_cnulltlb(addr); }

#define MMU_STOREC(x,y)			\
void mmu_store_##x##_memcheckc(void* addr, y v)	\
{ mmu_store_##x##_cnulltlb(addr, v); }	\

#define MMU_LOAD(x,y)			\
y mmu_load_##x##_memcheck(void* addr)	\
{ return mmu_load_##x##_objwide(addr); }

#define MMU_STORE(x,y)			\
void mmu_store_##x##_memcheck(void* addr, y v)	\
{ mmu_store_##x##_objwide(addr, v); }		\



#define MMU_ACCESS(x,y)			\
	MMU_LOAD(x,y)	MMU_LOADC(x,y)	\
	MMU_STORE(x,y)	MMU_STOREC(x,y)

MMU_ACCESS(8, uint8_t)
MMU_ACCESS(16, uint16_t)
MMU_ACCESS(32, uint32_t)
MMU_ACCESS(64, uint64_t)
MMU_ACCESS(128, __uint128_t)
