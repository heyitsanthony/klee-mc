// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc  -pipe-solver -use-sym-mmu -sym-mmu-type=../mmustack_stack.txt -sconc-mmu-type=stack2c - ./%t1  2>%t1.err >%t1.out
// RUN: ls klee-last | not grep smash.warning
// RUN: ls klee-last | not grep decode.err

#include "klee/klee.h"
#include <unistd.h>
#include <string.h>

int main(int argc, char *argv[])
{
	void		*p;
	uint32_t	buf[3];
	uint16_t	c;

	memset(buf, 0, sizeof(buf));

	/* test 1 */
	if (read(0, &c, 2) != 2) return 0;

	buf[c & 0x1] = 0xff;
	

	/* test 2 */
	return 3;
}
