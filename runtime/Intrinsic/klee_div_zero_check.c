//===-- klee_div_zero_check.c ---------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <klee/klee.h>

#include <stdint.h>

void klee_div_zero_check(uint64_t z) {
  if (z == 0)
    klee_uerror("divide by zero", "div.err");
}
