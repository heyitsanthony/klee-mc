// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -use-hwaccel=true -xchk-hwaccel=true -show-syscalls -pipe-solver - ./%t1 2>%t1.err >%t1.out
//
// If printf on a string literal is failing, I'd really like to know how!
// RUN: ls klee-last | not grep .err
//
#include <stdio.h>

int	x;

int main(int argc, char* argv[])
{
	int	i;
	for (i = 0; i < 5; i++) {
		write(1, "abc", 3);
		x = atoi("123");
	}

	return x;
}
