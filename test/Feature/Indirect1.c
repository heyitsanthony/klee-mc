// RUN: %llvmgcc %s -emit-llvm -g -I../../../include -c -o %t1.bc
// RUN: %klee %t1.bc
// RUN: ls klee-last | not grep err

#include "klee/klee.h"

int main()
{
	uint64_t v;

	v = 0;

	/* needs cast or the expression code gets confused with size
	 * mismatches */
	if (((uint32_t)klee_indirect1("klee_is_symbolic", v)))
		klee_uerror("expected non-symbolic", "indir1.err");

	v = klee_int("abc");
	if (!((uint32_t)klee_indirect1("klee_is_symbolic", v)))
		klee_uerror("expected symbolic", "indir1.err");

	return 0;
}
