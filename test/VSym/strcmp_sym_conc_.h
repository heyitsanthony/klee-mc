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
#ifdef USE_2X
	if (x != sc(s, "def123123123")) return 999;
#endif
	/* 3 tests */
	if (x < 0) return -1;
	if (x > 0) return 2;
	return 7;
}
