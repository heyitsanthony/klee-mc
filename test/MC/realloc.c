// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -use-symhooks - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
#include <stdlib.h>
#include <malloc.h>

int main(void)
{
	void	*x, *y;

	x = malloc(10);
	y = realloc(x, 20);
	memset(y, 0, 20);
	y = realloc(y, 30);
	memset(y, 0, 30);
	y = realloc(y, 256);
	memset(y, 0, 256);
	y = realloc(y, 4);
	memset(y, 0, 4);

	free(y);
	
	return 0;
}