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
#include <assert.h>
#include <stdint.h>
#include <string.h>

const char test_str[] = {"abcdefabcdef"};

int main(int argc, char* argv[])
{
	char	*s;
	s = strstr(test_str, "def");
	assert (((intptr_t)(s - test_str)) == 3);
	return 0;
}
