#include <stdint.h>
#include "klee/klee.h"

static int event_c = 0;

#define CHK_SYM(x)	\
	if (klee_is_symbolic(x)) klee_print_expr("SOFTFP symfp", x)

#define SOFTFP_ALERT()	klee_print_expr("SOFTFP event", event_c++);

#define UNOP_PRE(x,y)	\
void __hookpre_##x(uint##y##_t f) { SOFTFP_ALERT(); CHK_SYM(f); }

#define BINOP_PRE(x,y)	\
void __hookpre_##x(uint##y##_t f, uint##y##_t g) { SOFTFP_ALERT(); CHK_SYM(f); CHK_SYM(g); }

//#define UNOP_POST(x,y)	\
//void __hookpost_##x(uint##y##_t f) { ASSURE##y(f); }
#define UNOP_POST(x,y)

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