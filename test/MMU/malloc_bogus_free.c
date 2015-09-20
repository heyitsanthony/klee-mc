// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -dump-states-on-halt=false -stop-after-n-tests=3 -pipe-solver -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=../mmu_and_early_hookpass.txt - ./%t1 2>%t1.err >%t1.out
//
// Frees a bogus pointer, should trigger a leak error and a non-alloced error.
// The leak
// RUN:  ls klee-last/ | grep heap.err | wc -l | grep 1
// The malloc error for freeing a bad pointer (caught by earlyexit)
// RUN:  ls klee-last/ | grep malloc.err | wc -l | grep 1
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[])
{
	char* x = malloc(32);
	free(x+16);
	return 0;
}