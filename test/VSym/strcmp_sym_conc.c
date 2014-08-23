// RUN: gcc %s -O0 -o %t1
// RUN: rm -rf guest-vsymconc
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-vsymconc
// RUN: klee-mc -use-hookpass -guest-sshot=guest-vsymconc -guest-type=sshot -hookpass-lib=libvirtsyms.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
// RUN: ls klee-last | grep ktest | wc -l | grep 4
// RUN: ../../../scripts/get_all_returns.sh >%t1-rets 
// RUN: grep 0xffffff %t1-rets
// RUN: grep 0x2 %t1-rets
// RUN: grep 0x7 %t1-rets


#include <stdio.h>
#include <string.h>

typedef int(*sc_f)(const char* c, const char* c2);

int main(int argc, char* argv[])
{
	sc_f 	sc = strcmp;
	char	s[32];
	int	x;

	if (read(0, s, 32) != 32) return 1;
	s[31] = '\0';
	x = sc(s, "def123123123");

	/* 3 tests */
	if (x < 0) return -1;
	if (x > 0) return 2;
	return 7;
}
