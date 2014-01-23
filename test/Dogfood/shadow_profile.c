// RUN: gcc -O2 -I../../../include -o %t1 %s
// RUN: ./%t1
//
#define __KLEE_H__
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../../runtime/mmu/shadow.h"

#define klee_assert(x) assert(x)
#define klee_min_value(x) x
#define klee_max_value(x) x
#define klee_assume_eq(x,y) assert (x == y)
#define klee_valid_eq(x,y) (x == y)
#define klee_print_expr(x,y) printf("%s %lx\n", x, (uint64_t)(y))
#define klee_stack_trace() do {} while (0)
#define klee_is_symbolic(x) 0
#define klee_make_symbolic(x,y,z) do {} while (0)
#define klee_assume_ult(x,y) do {} while (0)
#define klee_assume_ne(x,y) do {} while (0)

#include "../../runtime/mmu/shadow.c"

struct shadow_info	si;
#define DAT_SZ		512
#define BASE_V		0x6000100

int main(void)
{
	uint64_t	dat[DAT_SZ];
	int		i;

	shadow_init(&si, 4, 32, 0);

	for (i = 0; i < DAT_SZ; i++) dat[i] = BASE_V+i*4;

	for (i = 0; i < DAT_SZ; i++) {
		int	j;

		printf("dat[%d] = %lx\n", i, dat[i]);
		for (j = 0; j < i; j++) { assert(shadow_get(&si, dat[j])); }
		for (j = i; j < DAT_SZ; j++) {
			if (shadow_get(&si, dat[j])) {
				printf("unexpected non-zero on j=%d\n", j);
				abort();
			}
		}

		shadow_put(&si, dat[i], 1);

	}

	return 0;
}
