// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep ptr.err
// RUN: grep "Hooked " %t1.err | grep malloc
// RUN: ls klee-last | grep heap.err
#include "klee/klee.h"
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	void		*x;
	unsigned	i;

	for (i = 0; i < 10; i++) {
		x = malloc(16);
		free(x);
	}
	x = malloc(16);
	((char*)x)[15] = 10;
	free(x);
	for (i = 0; i < 10; i++) {
		x = malloc(16);
		free(x);
	}

	return 0;
}