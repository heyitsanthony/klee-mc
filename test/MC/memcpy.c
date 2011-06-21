// RUN: gcc %s -O0 -o %t1
// RUN: klee-mc - ./%t1 2>%t1.err >%t1.out
//
// Concrete / no syscalls so there should only be one explored path.
// RUN: grep "explored paths = 1" %t1.err
//
// It should exit nicely
// RUN: grep "exitcode" %t1.err
//
// It should exit with the difference between the two sums of arrays (0)
// RUN: grep "exitcode=0" %t1.err

#include <string.h>

int x[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
int main(void)
{
  int y[sizeof(x)/sizeof(int)];
  int i, ret1, ret2;
  memcpy(y, x, sizeof(x));
  ret1 = 0;
  ret2 = 0;
  for (i = 0; i < sizeof(x)/sizeof(int); i++) {
  	ret1 += x[i];
	ret2 += y[i];
  }

  return (ret1 - ret2) + memcmp(x, y, sizeof(x));
}