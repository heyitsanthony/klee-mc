// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep ptr.err
// RUN: grep "Hooked " %t1.err | grep malloc
// RUN: ls klee-last | not grep heap.err
#include "klee/klee.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char *argv[])
{
	char	*x, *y;
	int	i;

	x = malloc(33);
	memset(x, 2, 33);
	y = realloc(x, 64);
	if (x != y)
		return 1;

	for (i = 0; i < 33; i++)
		x[0] = x[0] + x[i];
	free(x);

	return 0;
}