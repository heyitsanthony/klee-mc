// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -pipe-solver -use-symhooks - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | grep heap.err
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[])
{
	/* OH NO, BRO, NO! */
	char* x = malloc(10);
	x[-1] = '1';
	return 0;
}