// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
//
// RUN: ls klee-last | grep "badjmp.err"
//
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

	r = read(0, buf, 16);
	if (r <= 0) return -1;
	f = (void*)&buf;

	return f();
}