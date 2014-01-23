#include "klee/klee.h"
#include "cex.h"

void cex_init(struct cex_t* cex, unsigned default_entries)
{
	cex->cex_last_expr = 123;
	cex->cex_last_n = 0;
	cex->cex_max = default_entries;
	cex->cex_buf = malloc(cex->cex_max*sizeof(uint64_t));
}

int cex_take(struct cex_t* cex, uint64_t expr, unsigned i)
{
	unsigned k;

	/* feasible? */
	if (!cex->cex_last_n || i >= cex->cex_last_n) return 0;
	if (!klee_feasible_eq(expr, cex_get(cex, i))) return 0;

	/* remove element */
	for (k = i; k < cex->cex_last_n - 1; k++)
		cex->cex_buf[k] = cex->cex_buf[k+1];
	cex->cex_last_n--;

	return 1;
}

int cex_find(struct cex_t* cex, uint64_t expr)
{
	if (!klee_is_symbolic(expr)) {
		cex->cex_last_expr = expr;
		cex->cex_buf[0] = expr;
		cex->cex_last_n = 1;
		return 1;
	}

	if (cex->cex_last_n && klee_feasible_eq(cex->cex_last_expr, expr)) {
		unsigned	i;
		uint64_t	p = 0;
		for (i = 0; i < cex->cex_last_n; i++)
			p = klee_mk_or(p, klee_mk_eq(expr, cex_get(cex, i)));
		/* reuse underapproximation of cex from last klee_get_values */
		if (klee_feasible(p)) return cex->cex_last_n;
	}

	do {
		cex->cex_last_n = klee_get_values(
			expr, cex->cex_buf, cex->cex_max);
		if (cex->cex_last_n != cex->cex_max) break;

		cex->cex_max *= 2;
		free(cex->cex_buf);
		cex->cex_buf = malloc(sizeof(uint64_t) * cex->cex_max);
		continue;
	} while (1);

	cex->cex_last_expr = expr;
	return cex->cex_last_n;
}

void cex_fini(struct cex_t* cex) { free(cex->cex_buf); }


