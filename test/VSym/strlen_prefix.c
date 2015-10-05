// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -use-hookpass  -hookpass-lib=libvirtsyms.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ../../../scripts/get_all_returns.sh >%t1-rets 
// RUN: ls klee-last | not grep .err
// RUN: ls klee-last | grep ktest | wc -l | grep 4


#include <stdio.h>
#include <string.h>

typedef int(*sc_f)(const char* c);

int main(int argc, char* argv[])
{
	sc_f 		sc = strlen;
	char		s[32];
	unsigned	x, y;

	/* first test case */
	if (read(0, s, 32) != 32) return 1;
	s[31] = '\0';

	// x+4 == y
	x = sc(s);
	y = sc(s + 4);

	// possible; make sure s overlaps s+4
	if (x < 4) return 9;

	// all impossible
	if (x == y) return 2;
	if (x+1 == y) return 3;
	if (x+5 == y) return 4;

	if (x == 4) {
		// possible
		if (y == 0) return 5;
		// impossible
		if (y > 0) return 6;
	}

	// possible (e.g., x=5, y=1)
	return 7;
}
