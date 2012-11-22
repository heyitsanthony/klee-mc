#include "arith.h"

#define BOOTH_ONES_BEGIN	0
#define BOOTH_ONES_END		1
#define BOOTH_CONTINUE		2

#define booth_mul(SIZE,TYPE)				\
TYPE klee_arith_mul_booth_##SIZE(TYPE x, TYPE y)	\
{							\
	TYPE	cur_v = 0;			\
	int	last_bit = 0, bit_run_state;	\
	for (uint8_t k = 0; k < SIZE; k++) {	\
		TYPE	cur_mul;		\
		int	cur_bit;		\
\
		cur_bit = (x & (((TYPE)1) << k)) ? 1 : 0;	\
		if (last_bit != cur_bit) {			\
			bit_run_state = (cur_bit)		\
				? BOOTH_ONES_BEGIN		\
				: BOOTH_ONES_END;		\
		} else {					\
			bit_run_state = BOOTH_CONTINUE;		\
		}						\
\
		last_bit = cur_bit;	\
		if (bit_run_state == BOOTH_CONTINUE)	\
			continue;	\
\
		cur_mul = y << k;	\
\
		if (bit_run_state == BOOTH_ONES_END) {	\
			cur_v += cur_mul;	\
		} else if (bit_run_state == BOOTH_ONES_BEGIN) {	\
			cur_v -= cur_mul;	\
		} else {	\
			assert (0 == 1 && "WTF");	\
		}	\
	}		\
\
	/* cut off sign extension */	\
	if (last_bit) {			\
		cur_v += y << SIZE;	\
	}	\
\
	return cur_v;	\
}

booth_mul(8, uint8_t)
booth_mul(16, uint16_t)
booth_mul(32, uint32_t)
booth_mul(64, uint64_t)
booth_mul(128, __uint128_t)
