// RUN: gcc %s -m32 -O0 -o %t1
// RUN: SETENV VEXLLVM_32_ARCH 1
// RUN: klee-mc -pipe-solver -exit-on-error - ./%t1 2>%t1.err >%t1.out
// RUN: UNSETENV VEXLLVM_32_ARCH
// RUN: ls klee-last | not grep .err
#include <stdio.h>

int main(int argc, char* argv[])
{
	printf("Hello whirled\n");
	return 0;
}