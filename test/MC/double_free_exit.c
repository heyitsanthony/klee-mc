// RUN: gcc %s -I../../../include/ -O0 -o %t1
//
// Ensure we smash the stack in reality
// RUN: ./%t1 2>&1 | grep Oops
//
// KLEE should detect the smash.
// RUN: klee-mc -stop-after-n-tests=10 -use-hookpass -hookpass-lib=earlyexits.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | grep ktest.gz | wc -l | grep 1
// RUN: ls klee-last | grep malloc.err
#include <unistd.h>
#include <stdio.h>

int main(int argc, char* argv[])
{
	void	*x = malloc(123);
	free(x);
	free(x);
	return 0;
}
