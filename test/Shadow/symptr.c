// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -use-sym-mmu=false -pipe-solver -shadow-func-file=../bogus.txt -use-taint - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err

#include "klee/klee.h"
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	unsigned	vals[3] = {1, 2, 3};
	unsigned	sel_v;
	unsigned	v;

	if (read(0, &v, sizeof(v)) != sizeof(v))
		return 1;
	if (v >= 3)
		return 2;

	sel_v = vals[v];
	if (!ksys_is_shadowed(sel_v))
		ksys_error("symbolic read not shadowed", "noshadow.err");

	return 0;
}