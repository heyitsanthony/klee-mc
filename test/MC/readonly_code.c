// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -pipe-solver - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | grep .err
#include <stdio.h>

int f(void)
{
	return 123;
}

int main(int argc, char* argv[])
{
	char	*x = (char*)&f;
	x[0] = 11;
	return 0;
}
