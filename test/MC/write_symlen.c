// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc -pipe-solver - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
// RUN: ls klee-last | grep ktest | wc -l | grep 3

#include <unistd.h>

int main(void)
{
	int	n;
	char	what[512];

	if (read(0, &n, sizeof(n)) != sizeof(n))
		return -1;
	
	write(1, what, n);

	return 0;
}