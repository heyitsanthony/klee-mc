// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
//
// There should be pointer errors.
// RUN: ls klee-last | grep "ptr.err"
//
// There should be exactly *one* pointer error.
// RUN: ls klee-last | grep "ptr.err" | wc -l | grep 1
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

int main(void)
{
	char	*x = (char*)0;
	char	buf[16];
	int	r;

	buf[0] = 0;
	r = read(0, buf, 1);
	if (r <= 0) return -1;
	if (buf[0] != 0) *x = 1;

	return 0;
}