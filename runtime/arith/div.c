#include "arith.h"

#define div_loop(SIZE,TYPE)				\
TYPE klee_arith_div_loop_##SIZE(TYPE num, TYPE denom)	\
{	\
	TYPE	d;		\
	int	neg_num, neg_denom;	\
\
	if (denom == 0)			\
		klee_uerror("x / 0. Oops", "div.err");	\
\
	neg_num = (num < 0) ? 1 : 0;	\
	if (neg_num) num = -num;	\
\
	neg_denom = (neg_denom < 0) ? 1 : 0;	\
	if (neg_denom) denom = -denom;		\
\
	if (num < denom) return 0;		\
\
	for (d = 0; num > denom; num -= denom) d++;	\
\
	if (neg_num != neg_denom) d = -d;	\
\
	return d;	\
}

div_loop(8, uint8_t)
div_loop(16, uint16_t)
div_loop(32, uint32_t)
div_loop(64, uint64_t)
div_loop(128, __uint128_t)