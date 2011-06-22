// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
//
// Concrete / no syscalls so there should only be one explored path.
// RUN: grep "explored paths = 1" %t1.err
//
// It should exit nicely
// RUN: grep "exitcode" %t1.err
//
// It should exit with the string length
// RUN: grep "exitcode=0" %t1.err
//
// Cross check it too!
// RUN: klee-mc -xchkjit  - ./%t1 2>%t1.xchk.err >%t1.xchkout
//
// RUN: grep "VEXLLVM" %t1.xchk.err | grep "Exitcode=0"
#include <stdint.h>
#include <string.h>
#include <assert.h>

int main(int argc, char* argv[])
{
	uint8_t	x[37];
	int	i;
	memset(x, 0xa1, sizeof(x));
	for (i = 0; i < sizeof(x); i++) {
		assert (x[i] == 0xa1);
	}
	return 0;
}