#include "klee/klee.h"

/* 256mb */
#define MAX_PTR_RANGE		0x10000000

static struct kreport_ent testptr_ktab[] =
{	MK_KREPORT("address"),
	MK_KREPORT("example"),
	MK_KREPORT(NULL) };

int mmu_testptr_invalid(void* ptr)
{
	intptr_t	iptr = (intptr_t)ptr, iptr_c;

	iptr_c = klee_get_value(iptr);
	return ((iptr < 0x1000) |
		((iptr - iptr_c) > MAX_PTR_RANGE) |
		((iptr_c - iptr) > MAX_PTR_RANGE)); }

void mmu_testptr(void* ptr)
{
	if (!klee_prefer_true(mmu_testptr_invalid(ptr))) return;

#ifndef BROKEN_OSDI
	if (klee_feasible_ult(ptr, 0x1000))
		klee_assume_ult(ptr, 0x10000);
	else if (klee_feasible_ugt(ptr, 0x7fffffffffff))
		klee_assume_ugt(ptr, 0x7fffffffffff);
#endif
	/* still possible that pointer will resolve to valid range... oops? */

	SET_KREPORT(&testptr_ktab[0], ptr);
	SET_KREPORT(&testptr_ktab[1], klee_get_ptr(ptr));
	klee_uerror_details("bad memory access!", "ptr.err", &testptr_ktab); }