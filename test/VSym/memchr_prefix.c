// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -use-hookpass -hookpass-lib=libvirtsyms.bc - ./%t1 2>%t1.err >%t1.out
// RUN: grep "completed paths = 2" %t1.err
// RUN: grep "explored paths = 2" %t1.err
// RUN: grep "generated tests = 2" %t1.err


#include <stdio.h>
#include <string.h>
#include <stdint.h>

typedef void*	(*sc_f)(const void*, char, size_t sz);

int main(int argc, char* argv[])
{
	sc_f 	sc = memchr;
	char	s[32];
	void	*x;

	if (read(0, s, 32) != 32) return 1;
	s[31] = '\0';
	s[0] = 'e';
	s[1] = 'd';
	x = sc(s, 'd', sizeof(s));
	// if prefix checking works, will never try to evaluate+prune this path
	if (x != &s[1]) return 1;
	return 0;
}
