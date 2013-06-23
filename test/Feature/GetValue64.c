// RUN: %llvmgcc -c -I../../../include -o %t1.bc %s
// RUN: %klee --exit-on-error %t1.bc

#include "klee/klee.h"

#include <stdio.h>
#include <assert.h>
#include <stdint.h>

#define X_MIN 0xF000000000000000LL
#define X_MAX 0xF00000000000000FLL

int main() {
  uint64_t x;
  klee_make_symbolic(&x, sizeof x, "x");
  klee_assume_ugt(x, X_MIN);
  klee_assume_ule(x, X_MAX);

  assert(!klee_is_symbolic(klee_get_value(x)));
  assert(klee_get_value(x) > X_MIN);
  assert(klee_get_value(x) < X_MAX);

  return 0;
}
