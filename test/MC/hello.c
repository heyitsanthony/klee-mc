// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -pipe-solver - ./%t1 2>%t1.err >%t1.out
//
// If printf on a string literal is failing, I'd really like to know how!
// RUN: ls klee-last | not grep .err
#include <stdio.h>

int main(int argc, char* argv[])
{
	printf("Hello whirled\n");
	return 0;
}
