// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc  -use-hookpass -hookpass-lib=earlyexits.bc -print-new-ranges  -pipe-solver -use-sym-mmu -sym-mmu-type=../mmustack_stack.txt - ./%t1  2>%t1.err >%t1.out
// RUN: ls klee-last | grep decode.err$
// RUN: ls klee-last | egrep "(smash.warning|stackchk.err)"
// RUN: ls klee-last | grep badjmp.err

#include "klee/klee.h"
#include <unistd.h>
#include <string.h>

int main(int argc, char *argv[])
{
	uint32_t	buf[4];

	read(0, buf, sizeof(buf) * 8);

	/* test 2 */
	return 3;
}
