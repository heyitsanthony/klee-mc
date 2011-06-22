// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
// Two tests: wr < -1 and wr >= 0.
// RUN: ls klee-last/ | grep ktest | wc -l | grep 2
// (TODO: test that says no pointer errors)
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

int main(void)
{
	ssize_t wr;
	wr = write(1, NULL, 1);
	if (wr <= 0) return -1;
	return 0;
}