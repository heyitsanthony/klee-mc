// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -use-symhooks - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>

int main(void)
{
	void	*x, *y;
	char	s;
	ssize_t	br;

	br = read(0, &s, 1);
	if (br != 1)
		return 0;

	assert (br == 1);

	x = malloc(s);
	if (x == NULL)
		return 0;

	memset(x, 0, s);
	free(x);

	return 0;
}
