#ifndef _ESESTATS_H
#define	_ESESTATS_H

#include "ProfilingTimer.h"

#include <map>

namespace llvm {
    class Instruction;
}

namespace klee {

    class ESEStats {
    public:

        static unsigned updateCount;
        static ProfileTimer stackTimer;
        static ProfileTimer constraintTimer;
        static ProfileTimer addressSpaceTimer;
        static ProfileTimer checkTimer;
        static ProfileTimer terminateTimer;
        static ProfileTimer handleTimer;
        static ProfileTimer copyTimer;
        static ProfileTimer setupTimer;
        static ProfileTimer mergeTimer;
        static ProfileTimer reviseTimer;        
        static ProfileTimer totalTimer;

        static std::map<llvm::Instruction*, unsigned > forks;
        static unsigned termRecCount;

        static bool debug;
        static bool debugVerbose;
        static bool printStats;
        static bool printCoverStats;
    };

}

#endif	

