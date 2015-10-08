// RUN: %llvmgcc %s -emit-llvm -O0 -c -o %t1.bc
// RUN: %klee -do-partial-conc=1 %t1.bc 2>%t1.out.err
// RUN: ls klee-last | not grep assert.err

#include <assert.h>

int main() {
  int x, y, z = 0;
  klee_make_symbolic(&x, sizeof x, "x");
  klee_make_symbolic(&y, sizeof y, "y");
  assert(!klee_is_symbolic(z));
  klee_concretize_state(x);
  assert(!klee_is_symbolic(x));
  assert(klee_is_symbolic(y));
  return 0;
}
