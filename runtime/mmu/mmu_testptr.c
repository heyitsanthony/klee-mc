#include "klee/klee.h"
#include "mmu_testptr.h"

/* 256mb */
#define MAX_PTR_RANGE		0x10000000

void mmu_testptr(void* ptr)
{
	if (!mmu_testptr_invalid(ptr)) return;

	/* ACK! */
	klee_uerror("bad memory access!", "ptr.err");
}

int mmu_testptr_invalid(void* ptr)
{
	intptr_t	iptr = (intptr_t)ptr, iptr_c;

	if (iptr < 0x1000) return 1;

	iptr_c = klee_get_value(iptr);
	if ((iptr - iptr_c) > MAX_PTR_RANGE) return 1;
	if ((iptr_c - iptr) > MAX_PTR_RANGE) return 1;

	return 0;
}