#include "klee/klee.h"

int alarm(int n)
{
	klee_warning_once("ignoring alarm()");
	return 0;
}

uint64_t __klee_fork_all_n(uint64_t v, unsigned n)
{
	uint64_t	cur_v;
	unsigned	i;

	if (!klee_is_symbolic(v)) return v;

	n--;
	cur_v = klee_get_value(v);
	for (i = 0; i < n && !klee_fork_eq(cur_v, v); i++)
		cur_v = klee_get_value(v);

	klee_assume_eq(cur_v, v);
	return cur_v;
}


int klee_get_values(uint64_t expr, uint64_t* buf, unsigned n)
{
	uint64_t	pred = 1;
	unsigned	i;

	for (i = 0; i < n; i++) {
		uint64_t	c, c2;

		c = klee_get_value_pred(expr, pred);
		c2 = klee_get_value(expr);
		buf[i] = c;

		pred = klee_mk_and(pred, klee_mk_ne(expr, c));
		if (!__klee_feasible(pred)) {
			i++;
			break;
		}
	}

	return i;
}
