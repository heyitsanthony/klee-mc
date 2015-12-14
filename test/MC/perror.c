// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -pipe-solver -stop-after-n-tests=50 - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | grep .ktest.gz | wc -l | not grep 50
#include <stdio.h>

int main(int argc, char* argv[])
{
	perror("don't explode");
	return 0;
}
