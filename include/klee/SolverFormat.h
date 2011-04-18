//===-- SolverFormat.h ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_SOLVER_COMMON_H
#define KLEE_SOLVER_COMMON_H

#include <stdint.h>

namespace klee {

struct STPReqHdr {
  uint32_t timeout_sec;
  uint32_t timeout_usec;
  uint32_t length; // length in bytes of request (after packet header)
};

struct STPResHdr {
  char result;
  uint32_t rows; // number of CexItem's that accompany this header
};

struct CexItem {
  static const uint32_t const_flag = UINT32_C(1) << 31;
  uint32_t id;
  uint32_t offset;
  uint8_t value;
};

} // End klee namespace

#endif
