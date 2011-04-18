#ifndef _STATERECORDCOMPARER_H
#define	_STATERECORDCOMPARER_H

#include <set>
#include <vector>
#include <list>
#include <map>

namespace llvm {
    class Instruction;
}

namespace klee {
    class LiveSetCache;
    class StateRecord;
    class StateRecordManager;
    class ExecutionState;

    class StateRecordComparer {
    private:
        LiveSetCache* liveSetCache;
        std::vector<llvm::Instruction*> callstring;
        llvm::Instruction* inst;
        StateRecordManager* stateRecordManager;

        std::set<StateRecord*> pendingSet;
        std::set<StateRecord*> holdSet; //unexecuted
        std::set<StateRecord*> releaseSet; //unexecuted
        std::set<StateRecord*> terminatedSet;
        std::set<StateRecord*> prunedSet;

        unsigned prevCheckRepRecCount;

        void checkRep();

    public:

        void check(ExecutionState* state,
                std::set<ExecutionState*>& prunes,
                std::set<ExecutionState*>& releases,
                std::set<StateRecord*>& toTerminate);

        void prune(StateRecord* trec, StateRecord* toPrune,
                std::set<ExecutionState*>& prunes,
                std::set<ExecutionState*>& releases,
                std::set<StateRecord*>& toTerminate);

        void check(StateRecord* trec,
                std::set<ExecutionState*>& prunes,
                std::set<ExecutionState*>& releases,
                std::set<StateRecord*>& toTerminate);
        
        void unhold(StateRecord* rec, std::set<ExecutionState*>& releases); 
        void notifyNew(StateRecord* unxrec, std::set<ExecutionState*>& holds);
        void notifyExecuted(StateRecord* rec, std::set<ExecutionState*>& holds);
        void notifyTerminated(StateRecord* rec, std::set<ExecutionState*>& releases);
        void notifyReterminated(StateRecord* rec);

        StateRecordComparer(StateRecordManager* stateRecordManager, 
            const std::vector<llvm::Instruction*>& callstring,
            llvm::Instruction* inst);
        StateRecordComparer(StateRecordManager* stateRecordManager);
    };

}
#endif

