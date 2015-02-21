// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -pipe-solver -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | grep heap.err
#include <stdio.h>
#include <stdlib.h>

// Q: why does this fail under the current MMU code?
// A: because the MMU code marks all data as UNINIT on start
// writing to x[-1] (UNINIT) will not cause a problem. unlike (FREE)
// I looked into how glibc does malloc, there's a prev size and a current size
// in the header. However, a chunk's prev size can be marked 'invalid' even
// if there is an allocated chunk immediately before it in memory.
int main(int argc, char* argv[])
{
	char* x = malloc(10);
	x[10] = '1';
	free(x);
	return 0;
}