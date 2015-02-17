// RUN: gcc %s -O0 -o %t1
// RUN: rm -rf guest-vstrlen
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-vstrlen
// RUN: klee-mc -use-hookpass -guest-sshot=guest-vstrlen -guest-type=sshot -hookpass-lib=libvirtsyms.bc - ./%t1 2>%t1.err >%t1.out
// RUN: ../../../scripts/get_all_returns.sh >%t1-rets 
// RUN: ls klee-last | grep .err

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

	return s[-1];
}
