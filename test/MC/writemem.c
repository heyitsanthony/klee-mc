// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -write-mem -pipe-solver - ./%t1 2>%t1.err >%t1.out
//
// If printf on a string literal is failing, I'd really like to know how!
// RUN: ls klee-last | not grep .err
// RUN: ls klee-last/mem000001 klee-last/mem000002 | grep 601000
#include <stdio.h>

int main(int argc, char* argv[])
{
	char	x;
	read(0, &x, 1);
	return 0;
}
