// RUN: clang %s -I../../../include  -O0  -o %t1
// RUN: klee-mc  -quench-runaways=false  -use-stack-search -pipe-solver -max-time=30 -dump-stackstats=2 -stop-after-n-tests=1 -dump-states-on-halt=false - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | grep err
#include "klee/klee.h"

#define STACK_WASTE	8192
#define SYM_BYTES	512

char recur(char *sym, int len, int bit)
{
	char	blowout[STACK_WASTE];

	if (len == 0) return 0;

	blowout[STACK_WASTE-1] = sym[0];
	if (bit >= 8)
		return recur(sym+1, len-1, 0);

	if ((*sym) & (1 << bit)) {
		return STACK_WASTE-1;
	}

	return blowout[recur(sym, len, bit+1)];
}

int main()
{
	char sym[SYM_BYTES];
	int	ret;

	ret = read(0, sym, SYM_BYTES);
	ksys_print_expr("GOT READ", ret);
	if (ret != SYM_BYTES) {
		return 1;
	}

	recur(sym, SYM_BYTES, 0);
	return 0;
}
