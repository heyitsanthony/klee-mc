//===-- klee_range.c ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <assert.h>
#include <klee/klee.h>

int64_t klee_range(int64_t start, int64_t end, const char* name) {
  int64_t x;

  if (start >= end)
    klee_report_error(__FILE__, __LINE__, "invalid range", "user");

  if (start+1==end) {
    return start;
  } else {
    klee_make_symbolic(&x, sizeof x, name); 

    /* Make nicer constraint when simple... */
    if (start==0) {
      klee_assume((uint64_t) x < (uint64_t) end);
    } else {
      klee_assume(start <= x);
      klee_assume(x < end);
    }

    return x;
  }
}
