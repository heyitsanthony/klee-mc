/* stupid hack to fix busted llvm mem stats. */
#ifndef MEMUSAGE_H
#define MEMUSAGE_H

#include <sys/time.h>
#include <sys/resource.h>

#define USE_RUSAGE 1

static inline uint64_t getMemUsageKB(void)
{
#ifdef USE_RUSAGE
  struct rusage usage;
  int rc;
  rc = getrusage(RUSAGE_SELF, &usage);
  assert (rc == 0 && "RUSAGE FAILED??");
  return usage.ru_maxrss;
#else
#error Whoops, no way to get mem usage
  return 0;
#endif
}

static inline uint64_t getMemUsageMB(void) { return getMemUsageKB()/1024; }

#endif