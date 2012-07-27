// RUN: gcc %s -O3 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -use-sym-mmu - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err

/* this tests loading a primitive with the mmu */
/* right now it's not a very good test-- needs to 
 * check patterns; not 0 */
#include "klee/klee.h"
#include <stdint.h>
#include <string.h>
#include <unistd.h>

char x[4096];

#include <emmintrin.h>

int main(int argc, char *argv[])
{
	void		*p;
	uint8_t		c;
	__m128i		u128;

	p = x;
	if (read(0, &c, 1) != 1) return 1;

	p = ((char*)p + (c & ~0xf));
	memset(p, 1, 256);
	u128 = *((__m128i*)x);
	*((__m128i*)p) = u128;

	return 2;
}
