// RUN: gcc %s  -O0 -o %t1
// RUN: klee-mc -pipe-solver - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
// RUN: ls klee-last/ | grep ktest.gz | wc -l | grep 5
//
#include <unistd.h>

int main(int argc, char* argv[])
{
	/* fork calls clone, so three new states */
	if (fork() == 0) {
		/* st1, one test case */
		return 1;
	}

	/* two states: fork == -1, fork == parent */
	
	if (vfork() == 0) {
		/* each of two states from ^^^ get vfork == 0 */
		/* two test cases */
		return 2;
	}

	/* otherwise, two states with vfork == parent */
	return 3;
}
