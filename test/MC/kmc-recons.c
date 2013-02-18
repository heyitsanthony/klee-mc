// RUN: gcc %s -I../../../include/ -O0 -o %t1
// RUN: rm -rf guest-recons
// RUN: /usr/bin/env VEXLLVM_SAVEAS=guest-recons pt_run ./%t1
// RUN: klee-mc -pipe-solver -guest-type=sshot - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
// RUN: grep "exitcode=0" %t1.err
//
// Now, a replay test.
// RUN: SETENV KMC_RECONS_FILES 1
// RUN: kmc-replay 1 >%t1.replay.out 2>%t1.replay.err
// RUN: UNSETENV KMC_RECONS_FILES
// RUN: grep xitcode %t1.replay.err
// RUN: ls | grep "^recons" | wc -l | grep 3
// RUN: grep a recons.0
// RUN: grep b recons.1
// RUN: grep c recons.2
// RUN: rm -f recons.0 recons.1 recons.2
// RUN: rm -rf guest-recons
#include <stdio.h>
#include <klee/klee.h>

#define ksys_se()	ksys_indirect1("klee_silent_exit", 0)

int main(int argc, char* argv[])
{
	int	fd[3], i;
	char	x;

	fd[0] = open("asd", "wb");
	if (fd[0] < 0) ksys_se();
	fd[1] = open("abc", "wb");
	if (fd[1] < 0) ksys_se();
	fd[2] = open("asdd", "wb");
	if (fd[2] < 0) ksys_se();

	for (i = 0; i < 3; i++) {
		if (read(fd[i], &x, 1) != 1) ksys_se();
		if (x != ('a'+i)) ksys_se();
		close(fd[i]);
	}
	

	return 0;
}