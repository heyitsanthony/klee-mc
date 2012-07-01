// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver - ./%t1 2>%t1.err >%t1.out
//
// Should not have any errors.
// RUN: ls klee-last | not grep err

#include "klee/klee.h"
#include <unistd.h>
#include <sys/mman.h>

int main(int argc, char* argv[])
{
	void	*new_brk;

	new_brk = sbrk(0);
	if (new_brk == NULL || new_brk == (void*)-1) {
		ksys_error("bad initial brk query", "brk.err");
		return 0;
	}

	new_brk = sbrk(0x100);
	return 0;
}