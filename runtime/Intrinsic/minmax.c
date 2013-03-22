#include "klee/klee.h"

#define AVG(a,b)         (a & b) + ((a ^ b) >> 1)

uint64_t klee_min_value(uint64_t expr)
{
	uint64_t	upper_bound, lower_bound;
	static uint64_t	last_min_value = 128;

	if (klee_feasible_eq(expr, 0)) return 0;

	upper_bound = klee_get_value(expr);
	if (!klee_feasible_ult(expr, upper_bound))
		return upper_bound;

	lower_bound = 0;

	/* upper_bound <= last_min_value invalidates
	 * the last_min hint */
	if (last_min_value < upper_bound) {
		if (klee_valid_ule(last_min_value, expr)) {
			/* last_min_value <= expr => lower bound */
			lower_bound = last_min_value;
			if (klee_feasible_eq(last_min_value, expr)) {
				return lower_bound;
			}
		} else {
			/* last_min_value > expr => upper bound */
			upper_bound = last_min_value;
		}
	}

	while (upper_bound != lower_bound) {
		uint64_t	mid = AVG(upper_bound, lower_bound);

		if (klee_feasible_ule(expr, mid))
			upper_bound = mid;
		else
			lower_bound = mid + 1;
	}

	last_min_value = lower_bound;
	return lower_bound;
}

uint64_t klee_max_value(uint64_t expr)
{
	uint64_t	upper_bound, lower_bound;

	lower_bound = klee_get_value(expr);
	upper_bound = ~0ULL;

	if (!klee_feasible_ugt(expr, lower_bound))
		return lower_bound;

	while (upper_bound > lower_bound) {
		uint64_t	mid = AVG(upper_bound, lower_bound);

		if (klee_feasible_ugt(expr, mid)) {
			/* expr > mid implies max > mid */
			if (lower_bound == mid+1) return mid+1;
			lower_bound = mid + 1;
		} else {
			/* expr <= mid implies upper bound is mid */
			if (upper_bound == mid) break;
			upper_bound = mid;
		}
	}

	return upper_bound;
}

