// RUN: %llvmgcc %s -emit-llvm -O0 -c -o %t.bc
// We're using the uclibc pruning library, so stderr is redirected to stdout
// RUN: %klee --libc=uclibc --posix-runtime --init-env %t.bc --sym-files 0 10 --sym-stdout >%t.stdout 2>%t.log

// RUN: test -f klee-last/test000001.ktest.gz
// RUN: test -f klee-last/test000002.ktest.gz
// RUN: test -f klee-last/test000003.ktest.gz
// RUN: test -f klee-last/test000004.ktest.gz

// RUN: grep -q "stdin is a tty" %t.stdout
// RUN: grep -q "stdin is NOT a tty" %t.stdout
// RUN: grep -q "stdout is a tty" %t.stdout
// RUN: grep -q "stdout is NOT a tty" %t.stdout

#include <unistd.h>
#include <stdio.h>
#include <assert.h>

int main(int argc, char** argv) {
  int fd0 = 0; // stdin
  int fd1 = 1; // stdout

  int r = isatty(fd0);
  if (r)
    fprintf(stderr, "stdin is a tty\n");
  else fprintf(stderr, "stdin is NOT a tty\n");

  r = isatty(fd1);
  if (r)
    fprintf(stderr, "stdout is a tty\n");
  else fprintf(stderr, "stdout is NOT a tty\n");

  return 0;
}
