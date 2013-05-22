// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -mm-type=deterministic -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep ptr.err
// RUN: grep "Hooked " %t1.err | grep malloc
// RUN: ls klee-last | not grep heap.err
#include "klee/klee.h"
#include <stdlib.h>
#include <unistd.h>
#include <mcheck.h>

#define MAX_SZ	129

int main(int argc, char *argv[])
{

	mtrace();
	mcheck(NULL);

	int	i;
	volatile char *bufs[MAX_SZ];

	for (i = 1; i < MAX_SZ; i++)
		bufs[i-1] = malloc(i);

	for (i = 1; i < MAX_SZ; i++) {
		int	j;
		for (j = 0; j < i; j++)
			bufs[i-1][j] = i;
		free(bufs[i-1]);
	}

	return 0;
}