// RUN: %llvmgcc -c -I../../../include -o %t1.bc %s
// RUN: %klee --exit-on-error %t1.bc

#include "klee/klee.h"

#include <stdio.h>
#include <assert.h>
#include <stdint.h>

#define X_MIN 0xF000000000000000LL
#define X_MAX 0xF00000000000000FLL

int main() {
	uint64_t 	x;
	uint64_t	buf[16];
	int		n, i;

	klee_make_symbolic(&x, sizeof x, "x");
	klee_assume_ugt(x, X_MIN);
	klee_assume_ule(x, X_MAX);

	n = klee_get_values(x, buf, 16);
	/* values are [0xf0..01, 0xf0..0f] => 15 values */
	assert (n == 15);

	for (i = 0; i < n; i++) {
		assert (!klee_is_symbolic(buf[i]));
		assert (buf[i] > X_MIN && buf[i] <= X_MAX);
	}

	return 0;
}
