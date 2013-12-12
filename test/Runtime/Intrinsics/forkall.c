// RUN: %llvmgcc  -I../../include/ %s -emit-llvm -O0 -c -o %t.bc
// RUN: %klee --libc=none --no-externals %t.bc
// RUN: ls klee-last/ | grep  ktest.gz | wc -l | grep 10

#include <stdlib.h>
#include "klee/klee.h"

int main(int argc, char **argv)
{
	int	v;
	v = klee_range(0, 11, "len");
	__klee_fork_all_n(v, 10);
	return v;
}
