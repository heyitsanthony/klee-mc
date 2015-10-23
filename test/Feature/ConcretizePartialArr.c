// RUN: %llvmgcc %s -emit-llvm -O0 -c -o %t1.bc
// RUN: %klee -do-partial-conc=1 %t1.bc 2>%t1.out.err
// RUN: ls klee-last | not grep assert.err

#include <assert.h>
#include <string.h>

int main() {
	int	x, y;
	int	k[16], v;

	klee_make_symbolic(&x, sizeof x, "x");
	klee_make_symbolic(&y, sizeof y, "y");

	memset(k, 0, sizeof(k));
	k[9] = x;
	v = k[y & 0xf];
	klee_concretize_state(x);

	klee_print_expr("huh", v);

	// the symbolic in the symidx'd array should be concretized
	assert (klee_arr_count(v) == 1);

	klee_print_expr("huh2", klee_arr_count(v));

	return 0;
}
