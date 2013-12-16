// RUN: gcc %s -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -sym-mmu-type=ite -use-sym-mmu - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err
// MAX_ADDR + 1 test cases: failed read and values for ite read
// RUN: ls klee-last | not grep err
// RUN: ls klee-last | grep ktest.gz | wc -l | grep 17
//

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
	uint8_t		c, n;
	int		i;

	p = x;
	for (i = 0; i < 256; i++) x[i] = i;
	if (read(0, &c, 1) != 1) return 1;

	p = ((char*)p + c);
	n = *p;

	/* should fork MAX_ADDR times;
	 * 'c' is constrained by disjunction on deref */
	for (i = 0; i < 256; i++)
		if (c == i)
			return c;

	/* should never reach here */
	*((volatile char*)0) = 1;
	return c;
}
