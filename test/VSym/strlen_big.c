// RUN: gcc %s -O0 -o %t1
// RUN: rm -rf guest-vstrlen
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-vstrlen
// RUN: klee-mc -use-hookpass -guest-sshot=guest-vstrlen -guest-type=sshot -hookpass-lib=libvirtsyms.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ../../../scripts/get_all_returns.sh >%t1-rets 
// RUN: ls klee-last | not grep .err
// RUN: not grep 0xffffff %t1-rets
// RUN: not grep 0x7 %t1-rets
// RUN: grep 0x2 %t1-rets
// RUN: grep 0x1 %t1-rets
// RUN: grep 0x3 %t1-rets
// RUN: grep 0x4 %t1-rets
// RUN: ls klee-last | grep ktest | wc -l | grep 4

#include <stdio.h>
#include <string.h>

typedef int(*sc_f)(const char* c);

#define BUF_SZ	512

//
// This test has some history in that it breaks the somewhat inferior
// virtsym_safe_strcopy-- hit a symbolic => copy MAX_STR_LEN. The
// problem is that the string can be significantly longer than MAX_STR_LEN
// causing the vsym fini functions to run off the edge of the copied string.
//
int main(int argc, char* argv[])
{
	sc_f 		sc = strlen;
	char		s[BUF_SZ];
	unsigned	x;

	/* first test case */
	if (read(0, s, BUF_SZ) != BUF_SZ) return 1;
	s[BUF_SZ-1] = '\0';
	memset(s, '1', BUF_SZ/2);
	x = sc(s);

	/* should not happen */
	if (x == BUF_SZ+2) return -1;
	if (x < BUF_SZ/2) return -2;


	/* three tests */
	if (x == BUF_SZ/2) return 2;
	if (x == BUF_SZ/2+1) return 3;
	if (x >= BUF_SZ/2+2) return 4;

	/* x < BUF_SZ/2 but x[0 ... BUF_SZ/2-1] != 0 */
	return 7;
}
