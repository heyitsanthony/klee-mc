// RUN: %llvmgcc %s -emit-llvm -O0 -c -o %t.bc
// RUN: %klee --libc=uclibc --posix-runtime --init-env %t.bc --sym-arg 1 --sym-files 1 10 --sym-stdout 2>%t.log 
// RUN: test -f klee-last/test000001.ktest.gz
// RUN: test -f klee-last/test000002.ktest.gz
// RUN: test -f klee-last/test000003.ktest.gz

#include <stdio.h>       
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc, char** argv) {
  int fd = open(argv[1], O_RDWR);
  if (fd != -1)
    fprintf(stderr, "File opened successfully\n");
  else
    fprintf(stderr, "Cannot open file\n");

  if (fd != -1)
    close(fd);
}
