// RUN: gcc %s -I../../../include  -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
#include <string.h>
#include "klee/klee.h"

typedef int(*strcmp_f)(const char*, const char*);

int main(int argc, char* argv[])
{
	char		q[32];
	int		n;
	int		stack_v1, stack_v2, stack_v3;
	strcmp_f	f = strcmp;

	stack_v1 = ksys_indirect("klee_stack_depth");
	ksys_print_expr("v1", stack_v1);

	if (read(0, q, 31) != 31) return -1;
	q[31] = 0;

	stack_v2 = ksys_indirect("klee_stack_depth");
	ksys_print_expr("v2", stack_v2);

	if (stack_v1 != stack_v2)
		ksys_error("Wrong stack value", "stack.err");

	n = f("abc", "abcdeffffff");

	stack_v3 = ksys_indirect("klee_stack_depth");
	ksys_print_expr("v3", stack_v3);

	if (stack_v1 != stack_v3)
		ksys_error("Wrong stack value", "stack.err");

	return n;
}