// RUN: %llvmgcc %s -I../../../include -emit-llvm -g -c -o %t1.bc
// RUN: %klee -sym-mmu-type=uc -pipe-solver %t1.bc  2>%t.err
// RUN: ls klee-last | not grep mismatch.err
// RUN: ls klee-last | grep empty.err | wc -l | grep 1
// RUN: ls klee-last | grep ktest.gz | wc -l | grep 2
#include "klee/klee.h"

int main(void)
{
	char	*x, *y;
	int	i;

	klee_make_symbolic(&x, sizeof(x), "x");
	klee_assume_eq(((uint64_t)x) & 0x1, 0);
	y = (char*)(((uint64_t)x) & ~0x1ULL);

	for (i = 0; i < 4; i++) {
		if (x[i] != y[i]) {
			klee_uerror("mismatch", "mismatch.err");
		}
	}

	return 0;
}