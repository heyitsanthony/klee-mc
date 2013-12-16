// RUN: gcc %s -I../../../include/ -O0 -o %t1
// RUN: klee-mc -pipe-solver -symargs - ./%t1 asd asd 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err
// RUN: ls klee-last | grep ktest.gz | wc -l | grep 3
#include <stdint.h>
int main(int argc, char* argv[])
{
	char	*arg = argv[1];
	if (arg[1] == '0') return 1;
	if (arg[2] == '1') return 2;
	return 0;
}