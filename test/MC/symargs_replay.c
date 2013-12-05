// RUN: gcc %s -I../../../include/ -O0 -o %t1
// RUN: ../../../scripts/save_guest.sh ./%t1 guest-%t1
// RUN: klee-mc -pipe-solver -symargs - ./%t1 asd 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep err
// RUN: kmc-replay 1 >%t1.replay.out 2>&1
// RUN: grep Exit %t1.replay.out
// RUN: rm -rf guest-%t1
#include <stdint.h>
int main(int argc, char* argv[])
{
	char	*arg = argv[1];
	if (arg[1] == '0') puts("000");
	if (arg[2] == '1') puts("111");
	return 0;
}