// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc - ./%t1 2>%t1.err >%t1.out
// RUN: grep "Hooked " %t1.err | grep malloc
// RUN: ls klee-last | not grep .err
#include <stdlib.h>

int main(void)
{
	void	*x = malloc(10);
	int	u = malloc_usable_size(x);
	free(x);
	return u;
}