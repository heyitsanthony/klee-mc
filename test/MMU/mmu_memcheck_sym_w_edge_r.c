// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -mm-type=deterministic -pipe-solver -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep ptr.err
// RUN: grep "Hooked " %t1.err | grep malloc
// RUN: ls klee-last | grep heap.err
#include "klee/klee.h"
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	uint64_t	c;
	void		*x;

	if (read(0, &c, sizeof(c)) != sizeof(c)) return 0;

	x = malloc(16);
	((char*)x)[(c & 0xf)+1] = 10;
	free(x);

	return 0;
}