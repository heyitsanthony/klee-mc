// RUN: gcc %s -O0 -o %t1
//
// Don't crash on run
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
//
// Should have spit out a single pointer error
// RUN: ls klee-last | grep ptr.err | wc -l | grep 1
//
// RUN: not grep "IO_" klee-last/*ptr.err


#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[])
{
	char	*x = NULL;
	printf("hello\n");
	x[0] = 1;
	return 0;
}
