// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -dump-states-on-halt=false -stop-after-n-tests=3 -pipe-solver -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc - ./%t1 2>%t1.err >%t1.out
//
// Frees a bogus pointer, should trigger an error
// RUN: ls klee-last | grep heapfree.err
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[])
{
	char* x = malloc(32);
	free(x+16);
	return 0;
}