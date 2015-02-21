// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -use-yices2 -stop-after-n-tests=16 -pipe-solver -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>

int main(void)
{
	void	*x, *y;
	uint8_t	s;
	ssize_t	br;

	br = read(0, &s, 1);
	if (br != 1) return 0;
	assert (br == 1);

	s = s & 0xf;

	x = malloc(s);
	if (x == NULL)
		return 0;

	memset(x, 0, s);
	free(x);

	return 0;
}
