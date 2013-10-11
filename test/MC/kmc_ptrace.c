// RUN: gcc %s -I../../../include/ -O0 -o %t1
// RUN: rm -f guest-*
// RUN: /usr/bin/env VEXLLVM_SAVE=1 pt_run ./%t1
// RUN: klee-mc -guest-type=sshot - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | not grep .err
// RUN: grep "exitcode=0" %t1.err
//
// Now, a replay test.
// RUN: SETENV KMC_PTRACE 1
// RUN: kmc-replay 1 >%t1.replay.out 2>%t1.replay.err
// RUN: UNSETENV KMC_PTRACE
// RUN: grep xitcode %t1.replay.err
#include <sys/time.h>
#include <stdio.h>
#include <klee/klee.h>

int main(int argc, char* argv[])
{
	printf("work harder\n");
	return 0;
}