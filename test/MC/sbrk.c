// RUN: gcc -Werror %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver - ./%t1 2>%t1.err >%t1.out
//
// Should not have any errors.
// RUN: ls klee-last | not grep err

#include "klee/klee.h"
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>


static const intptr_t sizes[] = {0x100, 0x1000, 0x8000, 0x100000, 0 };

int main(int argc, char* argv[])
{
	char		*new_sbrk;
	unsigned	i;

	new_sbrk = sbrk(0);
	if (new_sbrk == NULL || new_sbrk == (void*)-1) {
		ksys_error("bad initial brk query", "brk.err");
		return 0;
	}

	for (i = 0; sizes[i]; i++) {
		memset(sbrk(sizes[i]), 1, sizes[i]);
		memset(sbrk(0) - sizes[i], 2, sizes[i]);
	}

	for (i = 0; sizes[i]; i++) {
		memset(sbrk(sizes[i]), 1, sizes[i]);
		memset(sbrk(0) - sizes[i], 2, sizes[i]);
		sbrk(-sizes[i]);
	}

	return 0;
}