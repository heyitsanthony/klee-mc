//===-- Statistic.h ---------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_STATISTIC_H
#define KLEE_STATISTIC_H

#include "llvm/Support/DataTypes.h"
#include <string>
#include <vector>

namespace klee {
  class Statistic;
  class StatisticManager;
  class StatisticRecord;

  /// Statistic - A named statistic instance.
  ///
  /// The Statistic class holds information about the statistic, but
  /// not the actual values.
  class Statistic {
    friend class StatisticManager;
    friend class StatisticRecord;

  private:
    unsigned			id;
    const std::string		name;
    const std::string		shortName;
    std::vector<uint64_t>	indexedStats;

  public:
    Statistic(const std::string &_name, 
              const std::string &_shortName);
    ~Statistic();

    /// getID - Get the unique statistic ID.
    unsigned getID() { return id; }

    /// getName - Get the statistic name.
    const std::string &getName() const { return name; }

    /// getShortName - Get the "short" statistic name, used in
    /// callgrind output for example.
    const std::string &getShortName() const { return shortName; }

    /// getValue - Get the current primary statistic value.
    uint64_t getValue() const;

    /// operator uint64_t - Get the current primary statistic value.
    operator uint64_t () const { return getValue(); }

    /// operator++ - Increment the statistic by 1.
    Statistic &operator ++() { return (*this += 1); }

    /// operator+= - Increment the statistic by \arg addend.
    Statistic &operator +=(const uint64_t addend);
    
    inline void incIndexedValue(unsigned index, uint64_t addend)
    {
	if (indexedStats.size() <= index) indexedStats.resize(index+1);
	indexedStats[index] += addend;
    }

    inline void setIndexedValue(unsigned index, uint64_t v)
    {
	if (indexedStats.size() <= index) indexedStats.resize(index+1);
	indexedStats[index] = v;
    }

   inline uint64_t getIndexedValue(unsigned index) const
   { return (indexedStats.size() <= index) ? 0 : indexedStats[index]; }

  };
}

#endif

