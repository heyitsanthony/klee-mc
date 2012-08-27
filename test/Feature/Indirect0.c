// RUN: %llvmgcc %s -emit-llvm -g -I../../../include -c -o %t1.bc
// RUN: %klee %t1.bc
// RUN: ls klee-last | not grep err

#include "klee/klee.h"

int main()
{
	klee_indirect0("klee_stack_trace");
	return 0;
}
