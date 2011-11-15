// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -pipe-solver -use-symhooks - ./%t1 2>%t1.err >%t1.out
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