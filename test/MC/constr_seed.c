// RUN: gcc %s -I../../../include -O0 -o %t1
// RUN: rm -rf ptrexprs
// RUN: mkdir ptrexprs
// RUN: klee-mc -pipe-solver -use-ivc=false -use-constr-seed -constrseed-solve - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
// RUN: klee-mc -pipe-solver -use-constr-seed - ./%t1 2>%t1.2.err >%t1.2.out
// RUN: ls klee-last | grep .err
#include "klee/klee.h"
#include <stdio.h>

int main(int argc, char* argv[])
{
	int	x;

	ksys_kmc_symrange(&x, 4, "x");

	/* note: we need to turn IVC off here to get the condition
	 * we want-- otherwise x is overwritten as '4' */
	ksys_assume_eq(x, 4);

	if (x == 4) {
		ksys_print_expr("B", x);
		return 0;
	}

	ksys_print_expr("A", x);
	return x;
}
