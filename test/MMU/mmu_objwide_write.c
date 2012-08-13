// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -use-sym-mmu -sym-mmu-type=objwide - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err

#include "klee/klee.h"
#include <unistd.h>

char x[4096*3];

int main(int argc, char *argv[])
{
	void		*p;
	uint16_t	c;

	if (read(0, &c, 2) != 2) return 0;
	if (c > (4096*3)-5) return;

	p = (x + c);
	*((uint8_t*)p) = 1;
	*((uint16_t*)p+1) = 2;
	*((uint32_t*)p+2) = 3;
	*((uint64_t*)p+3) = 4;

	return 0;
}
