// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc - ./%t1 2>%t1.err >%t1.out
// RUN: grep "Hooked " %t1.err | grep malloc
// RUN: ls klee-last | grep .err
#include <stdlib.h>

int main(void)
{
	char	*x = malloc(10);
	int	n;
	n = malloc_usable_size(x);
	free(x);
	x[0] = 1;
	return n;
}