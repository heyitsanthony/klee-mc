#ifndef _STATERECORDMANAGER_H
#define	_STATERECORDMANAGER_H

#include <list>
#include <map>
#include <set>
#include <vector>

namespace klee {
    class StateRecordComparer;
    class ExecutionState;
    class EquivalentStateEliminator;
    class StateRecord;
    class SegmentGraph;

    class StateRecordManager {
    public:
        SegmentGraph* segmentHoldGraph;
        private:
        std::map<unsigned, StateRecordComparer* > recCache;
        EquivalentStateEliminator* elim;

    public:
        StateRecordManager(EquivalentStateEliminator* elim);

        StateRecord* newStateRecord(ExecutionState* state,
            std::set<ExecutionState*>& holdSet);

        void unhold(StateRecord* unxrec, StateRecord* rec);

        bool attemptHold(StateRecord* unxrec, StateRecord* xrec);
        
        void terminate(StateRecord* toTerminate,
            std::list<StateRecord*>& terminated,
            std::set<ExecutionState*>& releases);

        void terminate(const std::set<StateRecord*>& toTerminate,
            std::list<StateRecord*>& terminated,
            std::set<ExecutionState*>& releases);

        void check(ExecutionState* state, 
            std::set<ExecutionState*>& prunedSet,
            std::set<ExecutionState*>& releasedSet,
            std::set<StateRecord*>& toTerminate);

        void check(StateRecord* rec, 
            std::set<ExecutionState*>& prunedSet,
            std::set<ExecutionState*>& releasedSet,
            std::set<StateRecord*>& toTerminate);

        void execute(StateRecord* rec, std::set<ExecutionState*>& holds);        

        StateRecordComparer* getComparer(ExecutionState* state);
    };
}

#endif

