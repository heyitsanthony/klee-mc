// RUN: gcc %s -I../../../include/ -O0 -o %t1
// RUN: rm -f guest-*
// RUN: /usr/bin/env VEXLLVM_SAVE=1 pt_run ./%t1
// RUN: klee-mc -logregs -guest-type=sshot - ./%t1 2>%t1.err >%t1.out
// RUN: ls klee-last | grep badtime.err
// RUN: ls klee-last | grep ktest.gz | wc -l | grep 2
// RUN: grep "exitcode=0" %t1.err
//
// Now, a replay test.
// RUN: kmc-replay 1 >%t1.replay.out 2>%t1.replay.err
// RUN: grep xitcode %t1.replay.err
#include <sys/time.h>
#include <stdio.h>
#include <klee/klee.h>

int main(int argc, char* argv[])
{
	struct timeval	tv;
	int		rc;

	rc = gettimeofday(&tv, NULL);
	if (rc != 0) {
		ksys_report_error(
			__FILE__, __LINE__,
			"gettimeofday failed",
			"badtime.err");
	}

	return 0;
}