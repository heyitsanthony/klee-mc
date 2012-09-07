// RUN: %llvmgcc -c -I../../../include -o %t1.bc %s
// RUN: %klee -write-paths -pipe-solver %t1.bc 2>%t1.1.err
// RUN: %klee -replay-path-dir=klee-last -exit-on-error %t1.bc 2>%t1.2.err
// RUN: ls klee-last/ | not grep .err
//

#include "klee/klee.h"

int main()
{
	uint64_t	x, last_x;
	int		i;

	klee_make_symbolic(&x, sizeof x, "x");
	last_x = klee_get_value(x);

	for (i = 0; i < 5; i++) {
		uint64_t	new_x = klee_get_value(x);
		if (last_x < x)
			new_x = klee_get_value(x);
		last_x = new_x;
	}

	klee_print_expr("last x", last_x);

	return 0;
}
