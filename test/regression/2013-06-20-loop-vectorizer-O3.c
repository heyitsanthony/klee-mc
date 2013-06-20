// RUN: %llvmgcc -I../../../include %s -emit-llvm -O3 -c -o %t1.bc
// RUN: %klee --exit-on-error %t1.bc
#include "klee/klee.h"
#include <stdlib.h>

unsigned n = 2;

int main() {
	unsigned i, x = 0;

        for(i = 0; i < sizeof(n) * 8; i++) {                                      
		if (n & (1 << i))
			x++;
	}        

	klee_assert(x == 1);
	return x;
}
