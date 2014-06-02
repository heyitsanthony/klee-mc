// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc  -pipe-solver -use-sym-mmu -sym-mmu-type=../mmustack_stack.txt - ./%t1  2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err$
// RUN: ls klee-last | grep warning

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

	buf[c & 0xf] = 213;
	

	/* test 2 */
	return 3;
}
