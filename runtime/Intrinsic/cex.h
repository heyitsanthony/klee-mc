#ifndef CEX_H
#define CEX_H

/* fast way to manage klee_get_values */

struct cex_t
{
	uint64_t	cex_last_expr;
	unsigned	cex_last_n;
	uint64_t	*cex_buf;
	unsigned	cex_max;
};

void cex_init(struct cex_t* cex, unsigned default_entries);
int cex_find(struct cex_t* cex, uint64_t expr);
int cex_take(struct cex_t* cex, uint64_t expr, unsigned i);
#define cex_get(c, i) (c)->cex_buf[i]
#define cex_n(c) (c)->cex_last_n
void cex_fini(struct cex_t* cex);
#define cex_flush(c)	(c)->cex_last_n = 0
#endif
