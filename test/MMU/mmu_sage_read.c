// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc  -pipe-solver -use-sym-mmu -sym-mmu-type=sage - ./%t1  2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err
// RUN: ls klee-last | grep ktest | wc -l | grep 3

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


	/* sage will fork here because it's unsound */
	if (what[c[0]] != what[c[1]]) {
		/* test 2 */
		return 1;
	}

	/* test 3 */
	return 3;
}
