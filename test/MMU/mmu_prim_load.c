// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -use-sym-mmu - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err

/* this tests loading a primitive with the mmu */
/* right now it's not a very good test-- needs to 
 * check patterns; not 0 */
#include "klee/klee.h"
#include <unistd.h>

char x[4096];

int main(int argc, char *argv[])
{
	void		*p;
	uint8_t		u8;
	uint16_t	u16;
	uint32_t	u32;
	uint64_t	u64;
	uint8_t		c;

	p = x;
	if (read(0, &c, 1) != 1) return 0;

	p = ((char*)p + c);
	u8 = *((uint8_t*)p);
	u16 = *((uint16_t*)p+1);
	u32 = *((uint32_t*)p+2);
	u64 = *((uint64_t*)p+3);

	ksys_assume(u8 == 0);
	ksys_assume(u16 == 0);
	ksys_assume(u32 == 0);
	ksys_assume(u64 == 0);

	return 0;
}
