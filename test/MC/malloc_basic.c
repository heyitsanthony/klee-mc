// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[])
{
	char* x = malloc(10);
	free(x);
	return 0;
}