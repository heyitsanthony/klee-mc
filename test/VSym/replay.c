// RUN: gcc %s -O0 -o %t1
// RUN: rm -rf guest-testreplay klee-testreplays
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-testreplay
// RUN: klee-mc -randomize-fork=false -use-hookpass -output-dir=klee-testreplays -guest-sshot=guest-testreplay -guest-type=sshot -hookpass-lib=libvirtsyms.bc - ./%t1 2>%t1.err >%t1.out
// RUN: rm -f klee-last
// RUN: ln -s klee-testreplays klee-last
// RUN: ls klee-last | not grep .err
// RUN: ../../../scripts/get_all_returns.sh >%t1-rets 
// RUN: grep 0xffffff %t1-rets
// RUN: grep 0x2 %t1-rets
// RUN: grep 0x7 %t1-rets
// RUN: grep 0x1 %t1-rets
// RUN: ls klee-last | grep ktest | wc -l | grep 5
// now make sure replay works...
// RUN: klee-mc -guest-sshot=guest-testreplay -guest-type=sshot -replay-ktest-dir=klee-testreplays -stop-after-n-tests=6 - 2>%t1.replay.err
// RUN: ls klee-last | not grep .err
// RUN: ls klee-last | wc -l | grep 6

#include <stdio.h>
#include <string.h>

typedef int(*sc_f)(const char* c, const char* c2);

int main(int argc, char* argv[])
{
	sc_f 	sc = strcmp;
	char	s[32], s2[32];
	int	x;

	/* 1 test */
	if (read(0, s, 32) != 32) return 1;

	s[31] = '\0';
	x = sc(s, "def123123123");

	/* 1 test */
	if (x < 0) return -1;

	/* 1 test */
	if (x > 0) return 2;

	/* create an extra symbolic for good measure */
	read(0, s2, 32);

	/* 2 tests */
	return 7;
}
