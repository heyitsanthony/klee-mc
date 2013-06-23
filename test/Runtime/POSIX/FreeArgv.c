// RUN: %llvmgcc %s -emit-llvm -O0 -c -o %t.bc
// RUN: %klee --init-env --posix-runtime %t.bc --sym-args 1 1 1
// RUN: ls klee-last | grep free.err | wc -l | grep "^3$"
// RUN: ls klee-last | grep ktest.gz | wc -l | grep "^4$"

int main(int argc, char **argv) {
  switch(klee_range(0, 3, "range")) {
  case 0:
    free(argv);
    break;
  case 1:
    free(argv[0]);
    break;
  case 2:
    free(argv[1]);
    break;
  }
  return 0;
}
