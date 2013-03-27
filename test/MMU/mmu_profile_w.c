// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -sym-mmu-type=profile -use-sym-mmu - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep ptr.err
// RUN: grep "Minimizing" %t1.err
// RUN: ls klee-last | grep ktest.gz | wc -l | grep 2
/* this tests loading a primitive with the mmu */
/* right now it's not a very good test-- but adding bits makes it slow-w-w-w */
#include "klee/klee.h"
#include <unistd.h>

uint64_t x[2] = { 0, 1 };


int main(int argc, char *argv[])
{
	uint64_t	c, r;
	unsigned	i;

	if (read(0, &c, sizeof(c)) != sizeof(c)) return 0;
	
	r = 0;
	for (i = 0; i < 4; i++) {
		r += x[(c >> i) & 1];
	}

	return r;
}
