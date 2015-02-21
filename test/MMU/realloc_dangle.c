// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -stop-after-n-tests=5 -dump-states-on-halt=false -pipe-solver -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | grep .err
#include <stdlib.h>
#include <malloc.h>

int main(void)
{
	void	*x, *y;

	x = malloc(10);
	y = realloc(x, 8);
	memset(y, 0, 8);
	free(y);
	free(x);
	
	return 0;
}
