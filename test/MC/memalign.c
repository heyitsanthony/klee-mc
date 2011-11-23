// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -use-symhooks - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
#include <stdlib.h>
#include <malloc.h>

int main(void)
{
	void	*y;

	y = memalign(4*sizeof(void*), 64 /* get 64 bytes */);
	free(y);
	
	return 0;
}