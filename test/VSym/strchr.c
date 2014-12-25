// RUN: gcc %s -O0 -o %t1
// RUN: rm -rf guest-vstrchr
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-vstrchr
// RUN: klee-mc -stop-after-n-tests=100 -use-hookpass -guest-sshot=guest-vstrchr -guest-type=sshot -hookpass-lib=libvirtsyms.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ../../../scripts/get_all_returns.sh >%t1-rets 
// RUN: ls klee-last | not grep .err
// RUN: not grep 0xffffff %t1-rets
// RUN: not grep 0x5 %t1-rets
// RUN: not grep 0x7 %t1-rets
// RUN: grep 0x2 %t1-rets
// RUN: grep 0x1 %t1-rets
// RUN: grep 0x3 %t1-rets
// RUN: grep 0x4 %t1-rets
// RUN: grep 0x8 %t1-rets
// RUN: not grep 0x9 %t1-rets
// RUN: ls klee-last | grep ktest | wc -l | grep 5


#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef char*	(*sc_f)(const char* s, char c);

int main(int argc, char* argv[])
{
	sc_f 	sc = strchr;
	char	s[32];
	char	*x;

	/* first test case */
	if (read(0, s, 32) != 32) return 1;
	s[31] = '\0';
	x = sc(s, 'a');

	/* should not happen */
	if (x == (s+34)) return -1;

	/* 1 test: could not find 'a' in string */
	if (x == NULL) return 2;

	if ((intptr_t)x < (intptr_t)s) return 5;

	/* should not happen */
	if (((uintptr_t)x) == 1) return 9;

	if (x == s) return 8;

	/* 1 test */
	if (x == (s+1)) return 3;
	/* 1 test */
	if (x >= (s+2)) return 4;

	/* should not happen */
	return 7;
}
