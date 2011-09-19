#ifndef _CALLSTRINGHASHER_H
#define	_CALLSTRINGHASHER_H

#include "klee/ExecutionState.h"
#include "static/Sugar.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"

#define ENABLE_HASH 1

#if ENABLE_HASH
#include <boost/functional/hash.hpp>
#endif

using namespace llvm;

namespace klee {

    class CallStringHasher {
    public:

        #if !ENABLE_HASH
        static unsigned hash(ExecutionState* es) {
            return 1;
        }
        #endif

        #if ENABLE_HASH
        static unsigned hash(ExecutionState* es) {
            size_t res = 0;
            boost::hash_combine(res, es->pc->inst);

            foreach(it, es->stack.begin(), es->stack.end()) {
                StackFrame* frame = &*it;
                boost::hash_combine(res, frame->kf->function);
                if (frame->caller) {
                    boost::hash_combine(res, frame->caller->inst);
                } else {
                    boost::hash_combine(res, 0);
                }
            }

            assert(res != 1);
            return res;
        }
        #endif
    };
}

#endif

