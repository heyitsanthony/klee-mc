// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -debug-print-values -pipe-solver -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep ptr.err
// RUN: grep "Hooked " %t1.err | grep malloc
// RUN: ls klee-last | not grep heap.err
#include "klee/klee.h"
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	void		*x;
	char		c = 0;
	x = malloc(16);
	if (read(0, x, 16) != 16) goto done;
	c = ((char*)x)[15];
done:
	free(x);
	return c;
}