// RUN: %llvmgcc -I../../klee/klee.h -c -o %t1.bc %s
// RUN: %llvmgcc -DHOOKS=1 -I../../klee/klee.h -c -o %t1.hooks.bc %s
// RUN: %klee -use-hookpass -hookpass-lib=./%t1.hooks.bc --exit-on-error %t1.bc >%t1.out 2>%t1.err
// RUN: grep "hookpre:0" %t1.err
// RUN: grep "hookpost:123" %t1.err
#include <stdio.h>
#include <assert.h>
#include "klee/klee.h"

#ifdef HOOKS
void __hookpre_main(void) { klee_print_expr("hookpre", 0); }
void __hookpost_main(int n) { klee_print_expr("hookpost", n); }
#else
int main()
{
	int x = klee_int("x");
	klee_assume_sgt(x, 10);
	klee_assume_slt(x, 20);

	assert(!klee_is_symbolic(klee_get_value(x)));
	assert(klee_get_value(x) > 10);
	assert(klee_get_value(x) < 20);

	return 123;
}
#endif
