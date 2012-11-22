#include "arith.h"

#define rem_loop(SIZE,TYPE)				\
TYPE klee_arith_rem_loop_##SIZE(TYPE num, TYPE denom)	\
{	\
	TYPE	d;	\
	int	neg_num, neg_denom;	\
\
	if (denom == 0)			\
		klee_uerror("x % 0. Oops", "div.err");	\
	neg_num = (num < 0) ? 1 : 0;	\
	if (neg_num) num = -num;	\
\
	neg_denom = (neg_denom < 0) ? 1 : 0;	\
	if (neg_denom) denom = -denom;	\
\
	if (num < denom) {	\
		if (neg_num != neg_denom) return -num;	\
		return num;	\
	}	\
\
	for (d = 0; num > denom; num -= denom) d++;	\
\
	if (neg_num != neg_denom) num = -num;	\
\
	return num;	\
}

rem_loop(8, uint8_t)
rem_loop(16, uint16_t)
rem_loop(32, uint32_t)
rem_loop(64, uint64_t)
rem_loop(128, __uint128_t)
