// RUN: gcc %s -O0 -I../../../include/  -o %t1

// RUN: klee-mc -pipe-solver -emit-all-errors -sym-mmu-type=minmaxa -use-sym-mmu - ./%t1 2>%t1.err >%t1.out
// one error state for min and one for max
// RUN: ls klee-last | grep ptr.err | wc -l | grep 2
// two error states, one ok state from the read() error
// RUN: ls klee-last | grep ktest.gz | wc -l | grep 3

/* this tests loading a primitive with the mmu */
/* right now it's not a very good test-- needs to 
 * check patterns; not 0 */
#include "klee/klee.h"
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>

int main(int argc, char *argv[])
{
	uint64_t	c;
	char		*v;

	if (read(0, &c, sizeof(c)) != sizeof(c)) return 0;

	v = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, -1);
	assert (v != NULL);
	
	v[(c & 0xffff) - 1] = 1;

	return 0;
}
