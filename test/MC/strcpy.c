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
#include <stdio.h>
#include <assert.h>
#include <string.h>

const char	s1[] = "HELLO COPY THIS STRING";

int main(int argc, char* argv[])
{
	char	s2[sizeof(s1)];
	int	i;

	strcpy(s2, s1);
	for (i = 0; s1[i]; i++) {
		assert (s2[i] == s1[i] && "CPY MISMATCH");
	}

	return 0;
}