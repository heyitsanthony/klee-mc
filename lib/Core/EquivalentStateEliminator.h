#ifndef _EQUIVALENTSTATEELIMINATOR_H
#define	_EQUIVALENTSTATEELIMINATOR_H

#include <sstream>
#include <map>
#include <set>

namespace klee {
    class KModule;
    class Executor;
    class StateRecord;    
    class ControlDependence;
    class StaticRecordManager;
    class StateRecordManager;
    class StateRecordComparer;

    class EquivalentStateEliminator {
    public:        
        Executor* executor;        
        StaticRecordManager* staticRecordManager;
        StateRecordManager* stateRecordManager;
        ControlDependence* controlDependence;
        std::set<ExecutionState*> allholds;
        KModule* kmodule;
        StateRecord* initialStateRecord;
        const std::set<ExecutionState*>& states;                

        EquivalentStateEliminator(Executor* _executor, KModule* kmodule, const std::set<ExecutionState*>& _states);
        
        void setup(ExecutionState* state, std::set<ExecutionState*>& holds);
        void complete();                
        void update(ExecutionState* current,
            std::set<ExecutionState*> &addedStates,
            std::set<ExecutionState*> &removedStates,
            std::set<ExecutionState*> &ignoreStates,
            std::set<ExecutionState*> &unignoreStates);
        void terminate(StateRecord* rec, std::list<StateRecord*>& newterm);

        void coverStats();
        void stats();

        

    };
}
#endif




