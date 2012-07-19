//===-- Statistics.cpp ----------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Statistics.h"

#include <assert.h>
#include <vector>

using namespace klee;

StatisticManager::StatisticManager()
: enabled(true)
, contextStats(0)
, index(0)
{
}

StatisticManager::~StatisticManager() {}

void StatisticManager::registerStatistic(Statistic &s)
{
  s.id = stats.size();
  globalStats.resize(s.id);
  memset(globalStats.data(), 0, sizeof(uint64_t)*s.id);
  stats.push_back(&s);
}

int StatisticManager::getStatisticID(const std::string &name) const {
  for (unsigned i=0; i<stats.size(); i++)
    if (stats[i]->getName() == name)
      return i;
  return -1;
}

Statistic *StatisticManager::getStatisticByName(const std::string &name) const {
  for (unsigned i=0; i<stats.size(); i++)
    if (stats[i]->getName() == name)
      return stats[i];
  return 0;
}


StatisticManager *klee::theStatisticManager = 0;

static StatisticManager &getStatisticManager() {
  static StatisticManager sm;
  theStatisticManager = &sm;
  return sm;
}
/* *** */

Statistic::Statistic(const std::string &_name, 
                     const std::string &_shortName) 
  : name(_name), 
    shortName(_shortName) {
  getStatisticManager().registerStatistic(*this);
}

Statistic::~Statistic() {
}

Statistic &Statistic::operator +=(const uint64_t addend)
{
  assert (this != NULL);
  getStatisticManager().incrementStatistic(*this, addend);
  return *this;
}

uint64_t Statistic::getValue() const {
  return theStatisticManager->getValue(*this);
}
