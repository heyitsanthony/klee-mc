#ifndef _STATICRECORD_H
#define	_STATICRECORD_H

#include <vector>
#include <set>
#include <map>
#include <string>

namespace llvm {
    class Function;
    class BasicBlock;
    class Instruction;
}

using namespace llvm;

namespace klee {
    class DependenceNode;
    class StaticRecordSCC;
    class KFunction;
    class KModule;    
    class StaticRecord;
    class StaticRecordManager;
    class StateRecord;

    class StaticRecordSCC {
    public:
        std::vector<StaticRecord*> elms;
        std::set<StaticRecordSCC*> succs;
        std::set<StaticRecordSCC*> preds;
        bool completed;

        StaticRecordSCC();
        bool haveAllCovered();
        bool haveAllSuccsCompleted();
        void print();
    };

    class StaticRecord {
    public:
        std::set<StateRecord*> sources;
        std::vector<Instruction*> insts;        
        std::set<StaticRecord*> succs;
        std::set<StaticRecord*> preds;
        std::set<StaticRecord*> control_succs;
        std::set<StaticRecord*> control_preds;

        KFunction* kfunction;
        Function* function;
        BasicBlock* basicBlock;
        unsigned index;
        bool covered;
        bool controlsExit;
        
        StaticRecord* ipostdom;
        bool iPostDomIsExit;
        bool iPostDomIsSuperExit;
        StaticRecordSCC* scc;

        static unsigned hash(StaticRecord* r1, StaticRecord* r2);

        bool isPHI();
        bool isReturn();
        bool isPredicate();
        std::string name();
        void cover(bool debug = false);

        void addControlSucc(StaticRecord * n);

        StaticRecord(KFunction* _kfunction, BasicBlock * _basicBlock, unsigned _index);
    };

    class StaticRecordManager {
    public:
        //std::map<Instruction*, StaticRecord*> m;
        std::map<Function*, StaticRecord*> entry;
        std::map<Function*, std::vector<StaticRecord*> > funrecs;
        KModule* kmodule;
        std::vector<StaticRecord*> nodes;
        std::vector<StaticRecordSCC*> sccs;

        void san(std::string& s);

        void writeCFGraph(Function* function);

        static void findDeepestIncomplete(StaticRecordSCC* scc);
        StaticRecordManager(KModule * _kmodule);
    };
}


#endif	

