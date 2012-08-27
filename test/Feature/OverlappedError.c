// RUN: %llvmgcc %s -I../../../include -g -emit-llvm -O0 -c -o %t1.bc
// RUN: %klee %t1.bc 2>%t1.err
// RUN: ls klee-last/ | grep .ktest | wc -l | grep 4
// RUN: ls klee-last/ | grep .ptr.err | wc -l | grep 2

#include "klee/klee.h"

#include <stdlib.h>

int main() {
  char *x = malloc(8);
  if (klee_range(0,2, "range")) {
    /* bad accesses: 5
     * good accesses: 0..4
     */
    *((int*) &x[klee_range(0,6, "range1")]) = 1;
  } else {
    /* bad accesses: -1
     * good accesses: 0..4
     */
    *((int*) &x[klee_range(-1,5, "range2")]) = 1;
  }
  free(x);
  return 0;
}
