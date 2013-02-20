// RUN: %llvmgcc %s -emit-llvm -O0 -c -o %t1.bc
// RUN: %klee %t1.bc
// RUN: test -f klee-last/test000001.free.err

int main() {
  int *x = malloc(4);
  free(x);
  free(x);
  return 0;
}
