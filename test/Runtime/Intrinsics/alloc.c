// RUN: %llvmgcc  -I../../include/ %s -emit-llvm -O0 -c -o %t.bc
// RUN: %klee --libc=none --no-externals %t.bc
// RUN: ls klee-last/ | not grep err

#include <stdlib.h>
#include "klee/klee.h"

int main(int argc, char **argv)
{
	int	len;
	char	*a;

	a = malloc(10);
	klee_assert (a != NULL);
	free(a);

	len = klee_range(1, 32, "len");
	a = malloc(len);
	if (a != NULL) free(a);

	klee_assert(malloc(~0) == NULL);

	return 0;
}
