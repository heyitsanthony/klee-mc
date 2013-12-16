// RUN: gcc %s -O3 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -sym-mmu-type=ite -use-sym-mmu - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err
// only two test cases: failed read and ite read
// RUN: ls klee-last | grep ktest.gz | wc -l | grep 2
//
// RUN: klee-mc -pipe-solver -sym-mmu-type=objwide -use-sym-mmu - ./%t1 2>%t1.objwide.err >%t1.objwide.out
// expect a failure on objwide as it unwisely makes c1 symbolic
// RUN: ls klee-last | grep err

/* this tests loading a primitive with the mmu */
/* right now it's not a very good test-- needs to 
 * check patterns; not 0 */
#include "klee/klee.h"
#include <stdint.h>
#include <string.h>
#include <unistd.h>

char x[4096];

int main(int argc, char *argv[])
{
	char		*p;
	uint8_t		c;
	int		i;

	p = x;
	memset(p, 0, 4096);
	memset(p, 1, 256);
	if (read(0, &c, 1) != 1) return 1;

	p = ((char*)p + c);
	c = *p;
	ksys_indirect2("klee_print_expr", "p", p);
	ksys_indirect2("klee_print_expr", "c1", c);
	if (ksys_indirect1("klee_is_symbolic", c))
		*((volatile char*)0) = 1;

	for (i = 0; i < 256; i++)
		x[i] = i;

	c = *p;
	ksys_indirect2("klee_print_expr", "c2", c);
	if (!ksys_indirect1("klee_is_symbolic", c))
		*((volatile char*)0) = 1;


	return c;
}
