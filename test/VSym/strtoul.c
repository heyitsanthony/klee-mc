// RUN: gcc %s -O0 -o %t1
// RUN: rm -rf guest-vstrtoul
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-vstrtoul
// RUN: klee-mc -use-hookpass -guest-sshot=guest-vstrtoul -guest-type=sshot -hookpass-lib=libvirtsyms.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ../../../scripts/get_all_returns.sh >%t1-rets 
// RUN: ls klee-last | not grep .err
// RUN: ls klee-last | grep ktest | wc -l | grep 5
// RUN: not grep 0xffffff %t1-rets
// RUN: grep 0x7 %t1-rets
// RUN: grep 0x2 %t1-rets
// RUN: grep 0x1 %t1-rets
// RUN: not grep 0x3 %t1-rets
// RUN: grep 0x4 %t1-rets


#include <stdio.h>
#include <stdlib.h>

typedef int(*sc_f)(const char* s, char** endptr, int base);

int main(int argc, char* argv[])
{
	sc_f 			sc = strtoul;
	char			s[32];
	unsigned long int	x;

	/* first test case */
	if (read(0, s, 32) != 32) return 1;
	s[31] = '\0';
	x = sc(s, NULL, 10);

	/* two tests */
	if (x < 10) return 2;
	if (x == 1) return 3; /* do not make, covered by x < 10 */
	if (x == ~0) return 4;

	return 7;
}
