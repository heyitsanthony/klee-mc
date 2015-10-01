// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -use-hookpass -hookpass-lib=libvirtsyms.bc - ./%t1 2>%t1.err >%t1.out
// RUN: grep "completed paths = 2" %t1.err
// RUN: grep "explored paths = 2" %t1.err
// RUN: grep "generated tests = 2" %t1.err


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
	s[0] = 'e';
	x = sc(s, "def123123123");
	if (x <= 0) return 1;
	return 0;
}
