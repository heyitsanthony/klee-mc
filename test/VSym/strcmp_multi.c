// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -use-hookpass -hookpass-lib=libvirtsyms.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
// RUN: ls klee-last | grep ktest | wc -l | grep 4

#include <stdio.h>
#include <string.h>

typedef int(*sc_f)(const char* c, const char* c2);

int main(int argc, char* argv[])
{
	sc_f 	sc = strcmp;
	char	s[32];
	int	x, y;

	// expected exit
	if (read(0, s, 32) != 32) return 9;
	s[31] = '\0';
	x = sc(s, "-help");
	y = sc(s, "-h");

	if (x == 0) {
		if (y == 0) return 1;
		if (y < 0) return 2;
		// 101 -- expected exit
		if (y > 0) return 3;
	}

	if (y == 0) {
		if (x == 0) return 4;
		// -101 -- expected exit
		if (x < 0) return 5;
		if (x > 0) return 6;
	}

	// x != 0 /\ y != 0; expected exit
	return 0;
}
