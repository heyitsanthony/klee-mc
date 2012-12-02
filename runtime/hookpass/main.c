#include <stdint.h>
#include "klee/klee.h"

/* f32
 * 1 bit - sign		0x80000000
 * 8 bits - exp		0x7f800000	
 * 23 bits - mant	0x007fffff
 */
#define ASSURE32(x)	klee_assume_gt(x & 0x7f800000, 0x7f800000)


/* f64
 * 1 bit - sign		0x80000000 0000 0000
 * 11 bits - exp	0x7ff00000 0000 0000
 * 52 bits - mant	0x000fffff ffff ffff
 */
#define ASSURE64(x)	klee_assume_gt(x&0x7ff0000000000000, 0x7ff0000000000000)

#define UNOP_PRE(x,y)	\
void __hookpre_##x(uint##y##_t f) { ASSURE##y(f); }

#define UNOP_POST(x,y)	\
void __hookpost_##x(uint##y##_t f) { ASSURE##y(f); }

#define BINOP_PRE(x,y)	\
void __hookpre_##x(uint##y##_t f) { ASSURE##y(f); }

#define UNOP(x,y,z) 	\
	UNOP_PRE(x,y)	\
	UNOP_POST(x,z)

#define BINOP(x,y)	\
	BINOP_PRE(x,y)	\
	UNOP_POST(x,y)

UNOP(float64_to_float32, 64, 32)
UNOP(float32_to_float64, 32, 64)
UNOP(int32_to_float32, 32, 32)
UNOP(int32_to_float64, 32, 64)
UNOP(int64_to_float32, 64, 32)
UNOP(int64_to_float64, 64, 64)
UNOP(float32_to_int32, 32, 32)
UNOP(float32_to_int64, 32, 64)
UNOP(float64_to_int32, 64, 32)
UNOP(float64_to_int64, 64, 64)
// UNOP_PRE(float32_is_nan, 32)
// UNOP_PRE(float64_is_nan, 64)

UNOP(float32_sqrt, 32, 32)
UNOP(float64_sqrt, 64, 64)

BINOP(float32_add, 32)
BINOP(float64_add, 64)
BINOP(float32_sub, 32)
BINOP(float64_sub, 64)
BINOP(float32_mul, 32)
BINOP(float64_mul, 64)
BINOP(float32_div, 32)
BINOP(float64_div, 64)
BINOP(float32_rem, 32)
BINOP(float64_rem, 64)

BINOP(float32_eq, 32)
BINOP(float64_eq, 64)
BINOP(float32_lt, 32)
BINOP(float64_lt, 64)
BINOP(float32_le, 32)
BINOP(float64_le, 64)