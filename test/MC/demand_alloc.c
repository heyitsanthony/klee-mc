// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc --mm-type=deterministic - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
//
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	unsigned i;

	for (i = 1; i < 128; i += 4) {
		char* p = malloc(1024*1024*i);
		p[0] = 1;
		p[1024*1024*i-1] = 1;
		free(p);
	}

	return 0;
}