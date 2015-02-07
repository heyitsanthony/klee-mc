// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -pipe-solver -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | grep .err
#include <stdlib.h>
#include <malloc.h>

int main(void)
{
	void	*x;
	char	*x_c;

	x = calloc(10, 4);
	x_c = x;
	x_c[40] = '\0'; // off by one
	free(x);

	return 0;
}
