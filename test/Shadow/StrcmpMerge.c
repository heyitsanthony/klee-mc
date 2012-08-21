// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -tmerge-func-file=../strcmp.txt -use-taint-merge - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err
// RUN: grep "completed paths = 2" %t1.err
#include <string.h>
#include "klee/klee.h"

typedef int(*strcmp_f)(const char*, const char*);

int main(int argc, char* argv[])
{
	char	q[32];
	int	n;
	strcmp_f	f = strcmp;

	if (read(0, q, 31) != 31) return -1;
	q[31] = 0;

	n = f(q, "you'll never get it!");

	if (n == 0) ksys_print_expr("got it", n);
	return n;
}