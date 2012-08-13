// RUN: gcc %s -I../../../include/ -O0 -o %t1
// RUN: klee-mc -use-ivc -pipe-solver  - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
#include "klee/klee.h"
#include <stdio.h>


void test_sym(uint64_t x)
{
	if (!ksys_is_sym(x))
		return;

	ksys_error("x should be constant", "ivc.err");
	_exit(2);
}


int main(int argc, char* argv[])
{
	uint64_t	x;

	if (read(0, &x, sizeof(x)) != sizeof(x)) return -1;

	if (x == 0) test_sym(x);
	else if (x == 1) test_sym(x);

	switch (x) {
	case 4: break;
	case 5: break;
	case 6: break;
	default: x = 0; break;
	}

	test_sym(x);
	return 0;
}
