// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
// RUN: klee-mc -pipe-solver -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc - ./%t1 2>%t1.err >%t1.out
// RUN: find klee-last/ | grep heap.err | xargs grep "Leaked"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[])
{
	char* x = malloc(10);
	return 0;
}