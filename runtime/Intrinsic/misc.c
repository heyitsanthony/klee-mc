#include "klee/klee.h"

int alarm(int n)
{
	klee_warning_once("ignoring alarm()");
	return 0;
}

uint64_t __klee_fork_all(uint64_t v)
{
	uint64_t	cur_v;

	if (!klee_is_symbolic(v)) return v;

	do { cur_v = klee_get_value(v); } while (!klee_fork_eq(cur_v, v));

	klee_assume_eq(cur_v, v);
	return cur_v;
}
