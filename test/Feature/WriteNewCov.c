// RUN: %llvmgcc %s -emit-llvm -O0 -c -o %t1.bc
// RUN: %klee %t1.bc
// RUN: ls klee-last | grep ktest.gz | wc -l | grep 3
// RUN: %klee -emit-only-covset-uncommitted %t1.bc
// will write out 1 or 2 states depending on order of evaluation, but never 3
// RUN: ls klee-last | grep ktest.gz | wc -l | not grep 3

#include <assert.h>

int f(char s)
{
	return s + 1;
}

int main()
{
	char s[16];
	klee_make_symbolic(&s, sizeof s);

	if (s[0] == '1') return f(s);
	else if (s[0] == '2') return f(s);

	return 0;
}
