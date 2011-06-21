// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
//
// Concrete / no syscalls so there should only be one explored path.
// RUN: grep "explored paths = 1" %t1.err
//
// It should exit nicely
// RUN: grep "exitcode" %t1.err
//
// It should exit with the string length
// RUN: grep "exitcode=10" %t1.err

#include <string.h>

char *x = "0123456789";	/* 10 characters */

int main(void)
{
  return strlen(x);
}
