#ifndef _LIVESETCACHE_H
#define	_LIVESETCACHE_H

#include "Location.h"

#include <map>
#include <set>


namespace klee {
    class ExecutionState;

    class StackLocation;
    class ObjectByteLocation;
    class StateRecord;
    class ArrayLocation;

    class LiveSet {
    public:
        std::vector<StackLocation > stackLocations;
        std::vector<ObjectByteLocation > objByteLocations;
        std::vector<ObjectLocation> objLocations;
        std::vector<ArrayByteLocation> arrByteLocations;
        std::vector<ArrayLocation> arrLocations;

        std::map<unsigned, std::set<StateRecord*> > recs;

    private:
        unsigned sz;
        bool szinit;

    public:

        LiveSet() : sz(0), szinit(false) {

        }

        unsigned size() {
            if (!szinit) {
                sz = stackLocations.size() + objByteLocations.size() + objLocations.size() + arrByteLocations.size() + arrLocations.size();
                szinit = true;
            }

            return sz;
        }
    };

    struct LiveSetLessThan {

        bool operator() (LiveSet* l1, LiveSet * l2) {
            return l1->size() < l2->size();
        }
    };

    class LiveSetCache {
    private:

        std::map<StackLocation, std::set<unsigned> > stackLocUnion;
        std::map<ObjectByteLocation, std::set<unsigned> > objByteUnion;
        std::map<unsigned, LiveSet*> cache;
        std::set<LiveSet*, LiveSetLessThan> liveSets;
        
    public:

        unsigned compares;
        unsigned visits;

        LiveSetCache();

        unsigned hash(ref<Expr> e);
        void readd(StateRecord* rec);
        void add(StateRecord* rec);
        StateRecord* check(ExecutionState* state);
        void printLiveSetStats();
    };
}

#endif	
