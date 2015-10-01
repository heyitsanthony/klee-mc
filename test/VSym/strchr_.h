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
#ifdef USE_2X
	if (sc(s, 'a') != x) return 999;
#endif

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
