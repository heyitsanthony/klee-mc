// RUN: gcc %s -O0 -o %t1
// RUN: rm -rf guest-vstrlen
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-vstrlen
// RUN: klee-mc -use-hookpass -guest-sshot=guest-vstrlen -guest-type=sshot -hookpass-lib=libvirtsyms.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ../../../scripts/get_all_returns.sh >%t1-rets 
// RUN: ls klee-last | not grep .err
// RUN: ls klee-last | grep ktest | wc -l | grep 4
// RUN: not grep 0xffffff %t1-rets
// RUN: not grep 0x7 %t1-rets
// RUN: grep 0x2 %t1-rets
// RUN: grep 0x1 %t1-rets
// RUN: grep 0x3 %t1-rets
// RUN: grep 0x4 %t1-rets

#include <sys/mman.h>
#include <stdio.h>
#include <string.h>

typedef int(*sc_f)(const char* c);

int main(int argc, char* argv[])
{
	sc_f 		sc = strlen;
	char		*s;
	unsigned	r[2];
	void		*buf;

	buf = mmap(NULL, 8192, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, -1, 0);
	munmap(buf, 4096);
	s = buf;
	s = &s[4096]; // s[-1] = CRASH

	/* first test case */
	if (read(0, s, 32) != 32) return 1;
	s[31] = '\0';
	r[0] = sc(s);
	r[1] = sc(s);

	/* should not happen */
	if (r[0] == 34) return -1;

	/* three tests */
	if (r[0] == 0) return 2;
	if (r[0] == 1) return 3;
	if (r[0] >= 2) return 4;



	/* should not happen */
	return 7;
}
