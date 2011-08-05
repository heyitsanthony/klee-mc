// RUN: gcc %s -I../../../include/ -O0 -o %t1
// RUN: klee-mc -symargs - ./%t1 asd 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err
#include <stdint.h>
int main(int argc, char* argv[])
{
	char	*arg = argv[1];
	if (arg[1] == '0') puts("000");
	if (arg[2] == '1') puts("111");
	return 0;
}