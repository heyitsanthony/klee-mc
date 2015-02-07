// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -pipe-solver -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
#include <stdlib.h>
#include <malloc.h>

int main(void)
{
	void	*x, *y;

	x = malloc(10);
	y = memalign(4*sizeof(void*), 64 /* get 64 bytes */);
	free(y);
	free(x);
	
	return 0;
}
