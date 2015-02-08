// RUN: %llvmgcc  -I../../include/ %s -emit-llvm -O0 -c -o %t.bc
// RUN: %klee --libc=none  --no-externals %t.bc 2>%t.err
// RUN: ls klee-last/ | not grep err

#include <stdlib.h>
#include "klee/klee.h"

char x[128];

int main(int argc, char **argv)
{
	memset(x, 4, 4);
	memset(x, 4, 5);
	return 0;
}
