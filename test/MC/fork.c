// RUN: gcc %s  -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
//
// RUN: ls klee-last | not grep .err
//
#include <unistd.h>

int main(int argc, char* argv[])
{
	if (fork() == 0) {
		return 1;
	}

	if (vfork() == 0) {
		return 1;
	}

	return 2;
}
