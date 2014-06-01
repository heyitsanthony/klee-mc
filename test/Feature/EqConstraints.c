// RUN: %llvmgcc %s -emit-llvm -g -c -o %t1.bc
// RUN: %klee --exit-on-error %t1.bc 2>%t1.err

#include <assert.h>
#include "klee/klee.h"

int x;

int main() { 
  x = klee_int("x");

// vaguely interesting: if assert at beginning of both blocks,
// llvm lifts the klee_constr_count to happen before the branch,
// giving klee_constr_count() == 0.
  if (x < 10) {
//	assert (klee_constr_count() == 1 && "x < 10");
  	klee_assume_eq(x, 0);
	assert (klee_constr_count() == 1);
  } else {
	assert (klee_constr_count() == 1 && "x >= 10");
  	klee_assume_eq(x, 11);
	assert (klee_constr_count() == 1 && "x == 11");
  }

  return 0;
}
