// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -sym-mmu-type=fork -use-sym-mmu - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | grep ptr.err
//
// RUN: klee-mc -pipe-solver -sym-mmu-type=uniqptr -use-sym-mmu - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | grep ptr.err

/* this tests loading a primitive with the mmu */
/* right now it's not a very good test-- needs to 
 * check patterns; not 0 */
#include "klee/klee.h"
#include <unistd.h>

int main(int argc, char *argv[])
{
	uint64_t	c;
	uint64_t	*p64;

	if (read(0, &c, sizeof(c)) != sizeof(c)) return 0;
	p64 = (void*)c;
	
	ksys_assume(*p64 == 0);

	return 0;
}
