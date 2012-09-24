// RUN: gcc %s  -O0 -o %t1
// RUN: klee-mc -pipe-solver - ./%t1 2>%t1.err >%t1.out
//
// RUN: ls klee-last | not grep ptr.err
// RUN: ls klee-last | not grep sc.err
// RUN: ls klee-last | grep symexec.err
//
#include <unistd.h>

int main(int argc, char* argv[])
{
	char	symbuf[32];

	execl("/bin/ls", "x", NULL);

	read(0, symbuf, 32);
	execl(symbuf, "x", NULL);

	return 0;
}