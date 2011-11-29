// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -use-symhooks - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
#include <stdlib.h>
#include <malloc.h>

int main(void)
{
	void	*x;

	x = calloc(10, 4);
	memset(x, 0, 40);
	free(x);

	x = calloc(4, 10);
	memset(x, 0, 40);
	free(x);

	return 0;
}