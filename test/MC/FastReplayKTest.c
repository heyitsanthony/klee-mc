// RUN: gcc %s -I../../../include -O0 -o %t2
// RUN: rm -rf fast-ktests/
// RUN: %klee-mc -pipe-solver -output-dir=fast-ktests - ./%t2 >%t3.log 2>%t4.stderr
// RUN: %klee-mc -dump-states-on-halt=false -pipe-solver -only-replay -replay-suppress-forks=false -replay-faster -replay-ktest-dir=fast-ktests - ./%t2 >%t5.log 2>%t6.stderr
// RUN: ls klee-last | not grep err
// RUN: grep "res:110" %t6.stderr
// RUN: grep "res:105" %t6.stderr

#include "klee/klee.h"

int main() {
  int res = 1;
  int x, y;

  if (read(0, &x, sizeof x) != sizeof(x)) return 1;
  if (read(0, &y, sizeof y) != sizeof(y)) return 2;

  if (x&1) res *= 2;
  if (x&2) res *= 3;
  if (x&4) res *= 5;

  // get forced branch coverage
  if ((x&2) != 0) res *= 7;
  if ((x&2) == 0) res *= 11;
  ksys_print_expr("res", res);
 
  return 0;
}
