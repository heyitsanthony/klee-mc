// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -symargc -pipe-solver - ./%t1 aaaaa aaaaaa aaaaa 2>%t1.err >%t1.out
//
// If printf on a string literal is failing, I'd really like to know how!
// RUN: ls klee-last | not grep .err
// RUN: ls klee-last | grep ktest | wc -l | grep 4
#include <stdio.h>

int main(int argc, char* argv[])
{
	if (argc == 0) {
		// do not allow argc=0 by default
		*((volatile char*)0) = 1;
		return 1;
	}
	if (argc == 1) return 999;
	if (argc == 2) return 11111;
	if (argc == 3) return 123123;
	return 0;
}
