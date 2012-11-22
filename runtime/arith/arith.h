#ifndef KLEE_ARITH_H
#define KLEE_ARITH_H

#include <stdint.h>

#define DEF_OP(op,impl,type,sz)	\
	type klee_arith_##x##_##y##_##sz##(type, type)

#define DEF_ALL(op, impl)		\
	DEF_OP(op, impl, uint8_t, 8)	\
	DEF_OP(op, impl, uint16_t, 16)	\
	DEF_OP(op, impl, uint32_t, 32)	\
	DEF_OP(op, impl, uint64_t, 64)	\
	DEF_OP(op, impl, __uint128_t, 128)

DEF_ALL(mul, booth)
DEF_ALL(div, loop)
DEF_ALL(rem, loop)
