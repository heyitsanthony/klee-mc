// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -mm-type=deterministic -sym-mmu-type=memcheck -sconc-mmu-type=memcheckc -use-sym-mmu -use-hookpass -hookpass-lib=libkleeRuntimeMMU.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep ptr.err
// RUN: ls klee-last | not grep heap.err
// RUN: grep "Hooked " %t1.err | grep malloc
#include "klee/klee.h"
#include <stdlib.h>
#include <unistd.h>

#define CHUNKS		100
#define CHUNKSZ		63

int x = CHUNKSZ;

int main(int argc, char *argv[])
{
	int	i;
	volatile char *bufs[CHUNKS];

	for (i = 0; i < CHUNKS; i++)
		bufs[i] = malloc(CHUNKSZ);

	for (i = 0; i < CHUNKS; i++) {
		memset(bufs[i], 0xff, x);
		free(bufs[i]);
	}

	return 123;
}