#ifndef _ESESUPPORT_H
#define	_ESESUPPORT_H

#include <set>
#include <vector>

namespace llvm {
    class Instruction;
}

namespace klee {
    class ExecutionState;
    class StateRecord;

    class ESESupport {
    public:

        static void checkControlDependenceStack(ExecutionState* state);
        static bool isBBEntry(llvm::Instruction* inst);
        static bool isRecStart(ExecutionState* state);        
    };
}


#endif

