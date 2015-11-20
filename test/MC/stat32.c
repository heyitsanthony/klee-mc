// RUN: gcc %s -m32 -O0 -o %t1
// RUN: SETENV GUEST_32_ARCH 1
// RUN: klee-mc -pipe-solver - ./%t1 2>%t1.err >%t1.out
// RUN: UNSETENV GUEST_32_ARCH
// RUN: ls klee-last | not grep .err
//
// RUN: gcc -m64 %s -O0 -o %t1
// RUN: klee-mc -pipe-solver - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err


#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

struct stat_cage
{
	char		a[32];
	struct stat	s;
	char		b[32];
};

int main(int argc, char* argv[])
{
	struct stat_cage	sc;
	char			*s;
	int			i;

	memset(&sc, 0, sizeof(sc));
	stat("abc.sym", &sc.s);
	s = (char*)&sc;
	for (i = 0; i < sizeof(sc.a); i++)
		if (sc.a[i]) *((volatile char*)0) = 0;
	for (i = 0; i < sizeof(sc.b); i++)
		if (sc.b[i]) *((volatile char*)0) = 0;

	return 0;
}
