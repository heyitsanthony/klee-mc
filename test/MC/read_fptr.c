// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
//
// There should be no pointer errors.
// RUN: ls klee-last | not grep "ptr.err"
//
// RUN: grep "exitcode=1" %t1.err
// RUN: grep "exitcode=2" %t1.err
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

typedef int(*fptr)(void);

int f1(void) { return 1; }
int f2(void) { return 2; }

int main(void)
{
	char	*x = (char*)0;
	char	buf[16];
	int	r;
	fptr	f;

	buf[0] = 0;
	r = read(0, buf, 1);
	if (r <= 0) return -1;
	f = (buf[0] != 0) ? f1 : f2;

	return f();
}