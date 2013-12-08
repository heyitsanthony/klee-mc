// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -pipe-solver - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | grep decode.err | wc -l | grep 1

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[])
{
	__asm__ __volatile__("ud2");
	return 0;
}
