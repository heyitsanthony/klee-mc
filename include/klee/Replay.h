#ifndef KLEE_REPLAY_H
#define KLEE_REPLAY_H

namespace klee
{
#ifdef INCLUDE_INSTR_ID_IN_PATH_INFO
typedef std::pair<unsigned, unsigned> ReplayNode;
#else
typedef unsigned ReplayNode;
#endif

typedef std::vector<ReplayNode> ReplayPathType;
typedef std::list<ReplayPathType> ReplayPaths;
}

#endif
