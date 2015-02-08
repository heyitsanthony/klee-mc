// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -mm-type=deterministic -pipe-solver -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep ptr.err
// RUN: grep "Hooked " %t1.err | grep malloc
// RUN: ls klee-last | not grep heap.err
#include "klee/klee.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int(*sc_f)(const char* c, const char* c2);


int main(int argc, char *argv[])
{
	char	*x;
        sc_f    sc = strcmp;

	x = malloc(4);
	memset(x, 0, 4);
	x[0] = 1;
	if (sc(x, "abcdefghi") == 0)
		return 1;
	free(x);

	return 0;
}