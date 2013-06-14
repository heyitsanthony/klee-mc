// RUN: gcc -I../../../include %s -O0 -o %t1
// RUN: rm -rf guest-writesym guest-last
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-writesym
// RUN: klee-mc -validate-test -pipe-solver -guest-type=sshot - 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
// RUN: ls klee-last | grep ktest | wc -l | grep 3
// RUN: rm -rf guest-writesym guest-last

#include "klee/klee.h"
#include <unistd.h>

int main(void)
{
	int	n;
	char	what[512];

	if (read(0, &n, sizeof(n)) != sizeof(n))
		return -1;
	
	ksys_assume_ne(n, 0);
	
	n = write(1, what, n);

	return n;
}