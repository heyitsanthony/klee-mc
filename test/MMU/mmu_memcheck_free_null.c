// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep ptr.err
// RUN: ls klee-last | not grep heap.err
// RUN: grep "Hooked " %t1.err | grep free
#include "klee/klee.h"
#include <stdlib.h>
#include <unistd.h>

void	*x = NULL;

int main(int argc, char *argv[])
{
	free(x);
	return 0;
}
