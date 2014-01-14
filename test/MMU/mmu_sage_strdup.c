// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -show-syscalls -stop-after-n-tests=100 -pipe-solver -use-sym-mmu -sym-mmu-type=sage - ./%t1  2>%t1.err >%t1.out


#include "klee/klee.h"
#include <unistd.h>
#include <string.h>

char what[256];

int main(int argc, char *argv[])
{
	void		*p;
	uint8_t		c[2];

	memset(what, 0, sizeof(what));

	/* test 1 */
	if (read(0, &c, 2) != 2) return 0;

	p = strdup(&what[c[0]]);
	return 0;
}
