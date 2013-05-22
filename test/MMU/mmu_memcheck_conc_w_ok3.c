// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -mm-type=deterministic -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep ptr.err
// RUN: ls klee-last | not grep heap.err
// RUN: grep "Hooked " %t1.err | grep malloc
#include "klee/klee.h"
#include <stdlib.h>
#include <unistd.h>

#define MAX_SZ	129

static void f(int i)
{
	volatile char *x;
	int		j;

	x = malloc(i);
	for (j = 0; j < i; j++)
		x[j] = j;
	for (j = i - 1; j >= 0; j--)
		x[j] = j+1;
	free(x);
}

int main(int argc, char *argv[])
{
	int	i;

	for (i = 1; i < MAX_SZ; i++) f(i);
	for (;  i > 0; i--) f(i);

	return 123;
}