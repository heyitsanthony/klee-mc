// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -use-hwaccel=true -show-syscalls -pipe-solver - ./%t1 2>%t1.err >%t1.out
//
// If printf on a string literal is failing, I'd really like to know how!
// RUN: ls klee-last | not grep .err
#include <stdio.h>

int main(int argc, char* argv[])
{
	char	c;

	printf("Hello whirled\n");
	read(0, &c, 1);
	printf("Hello whirled\n");

	return 0;
}
