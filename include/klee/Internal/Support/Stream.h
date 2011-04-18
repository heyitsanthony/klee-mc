//===-- Stream.h ------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_UTIL_STREAM_H
#define KLEE_UTIL_STREAM_H

#include <ios>

namespace klee
{
  namespace util
  {
    class IosStateSaver
    {
      std::ios_base* ios_;
      std::ios_base::fmtflags flags_;
      std::streamsize prec_;
    public:
      IosStateSaver(std::ios_base& ios) : ios_(&ios), flags_(ios.flags()), prec_(ios.precision()) { }
      ~IosStateSaver() { ios_->flags(flags_); ios_->precision(prec_); }
    };
  }
}

#endif
