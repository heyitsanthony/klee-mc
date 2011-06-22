// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
//
// Concrete / no syscalls so there should only be one explored path.
// RUN: grep "explored paths = 1" %t1.err
//
// It should exit nicely
// RUN: grep "exitcode" %t1.err
//
// It should exit with the strchr
// RUN: grep "exitcode=3" %t1.err
//
// Cross check it too!
// RUN: klee-mc -xchkjit  - ./%t1 2>%t1.xchk.err >%t1.xchkout
//
// RUN: grep "VEXLLVM" %t1.xchk.err | grep "Exitcode=3"

#include <string.h>

char *x = "0123456789";	/* 10 characters */

int main(void)
{
  return strchr(x, '3')-x;
}
