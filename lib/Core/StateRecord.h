#ifndef _STATERECORD_H
#define	_STATERECORD_H

#include <set>
#include <list>
#include "DependenceNode.h"
#include "klee/Constraints.h"
#include "Sugar.h"
#include "klee/Internal/Module/KModule.h"

#include <map>

namespace llvm {
    class Instruction;
}

namespace klee {

    class EquivalentStateEliminator;
    class ExecutionStateData;
    class StateRecordComparer;

    struct ExprLessThan {

        bool operator() (const ref<Expr>& l1, const ref<Expr>& l2) {
            return l1->compare(*l2) == -1;
        }
    };

    typedef std::set<ref<Expr>, ExprLessThan> exprset_ty;
    typedef std::map< MallocKeyOffset, exprset_ty > como2cn_ty;
    typedef std::map< MallocKey, exprset_ty > somo2cn_ty;

    class StateRecord {
        //friend class StateRecordManager;
        //friend class StateRecordComparer;
        
    private:
        
        bool executed;
        StateRecordComparer* comparer;
        bool pruned;
  
        std::map<DependenceNode*, std::set<DependenceNode*> > copymap;
        std::set<StateRecord*> prunedSet;
        
    public:
        bool isShared;
        unsigned lochash;
        unsigned valhash;
        StaticRecord* staticRecord;
        static unsigned inleafcount;
        static unsigned count;
        static unsigned size;
        
        std::set<DependenceNode*> writes;
        std::set<DependenceNode*> curreads;
        std::vector<DependenceNode*> liveReads;
        std::set<StackWrite*> liveControls;
        StackWrite* branchRead;

        llvm::Instruction* inst;
        EquivalentStateEliminator* elim;
        StateRecord* parent;
    private:
        std::set<StateRecord*> children;
    public:
        unsigned hash;
        KFunction* kf;
        unsigned call;
        unsigned sfi;
        Instruction* curinst;

        ConstraintManager constraints;
        std::vector<Instruction*> callers;
        como2cn_ty como2cn;
        somo2cn_ty somo2cn;

        bool terminated;

        std::vector<ref<Expr> > liveConstraints;

        StackWrite* regularControl;
        bool isExitControl;
    private:
        ExecutionState* state;
        bool reterminated;
    public:
        std::set<unsigned> segments;
        unsigned currentSegment;
        static unsigned segmentCount;
        StateRecord* holder;

    public:

        void addChild(StateRecord* rec);
        bool isPruned();
        bool isTerminated();
        ExecutionState* getState();
        void setReterminated();
        void clearReterminated();
        void clearState();

        bool isExecuted();
        void setExecuted();

        static void getLiveConstraints(const ConstraintManager& constraints,
                const std::set<MallocKeyOffset>& liveMallocKeyOffsets,
                const std::set<MallocKey>& liveMallocKeys,
                std::vector<ref<Expr> >& result);

        StateRecordComparer* getComparer();
        void execute();
        void getLiveConstraints(const ConstraintManager& constraints, std::vector<ref<Expr> >& result);
        void clear();
        void printCallString();
        void printCurControls();
        void printReads();
        void reterminate(unsigned depth = 0);
        void print();
        void printControls();
        void addToPrunedSet(StateRecord* rec);
        void setPruned();
        void terminate();
        void printPathToExit();
        bool haveAllChildrenTerminatedOrPruned();
        void recopyLiveReadsInto(StateRecord* rec);
        //void checkForUnintializedReads(ReadNode* rd);        
        StateRecord* split();
        bool checkRep();
        void split(ExecutionState* s1, ExecutionState* s2);
        static bool equals(ref<Expr> exp1, ref<Expr> exp2);
        bool equivAddressSpace(ExecutionState * es2);
        bool equivStack(ExecutionState* es2);
        bool isEquiv(ExecutionState* esi2);
        void copyLiveReadsInto(ExecutionState* state);
        bool equivConstraints(ExecutionState* es2);
        void getLiveConstraints(const como2cn_ty& como2cn, const somo2cn_ty& somo2cn, exprset_ty& result);
        StateRecord(EquivalentStateEliminator* elim, ExecutionState* _state, StateRecordComparer* comparer);
        ~StateRecord();

        ConOffArrayAlloc* conOffArrayAlloc(const ExecutionState* state, StateRecord* allocRec, const MallocKey& mallocKey, unsigned offset);
        SymOffArrayAlloc* symOffArrayAlloc(const ExecutionState* state, StateRecord* allocRec, const MallocKey& mallocKey);
        void conOffObjectRead(const ObjectState* objectState, unsigned offset);
        void symOffObjectRead(const ObjectState* objectState);
        void stackRead(StackWrite* sw);
        void arrayRead(const ExecutionState* state, ref<ReadExpr> e);
        StackWrite* stackWrite(KFunction* _kf, unsigned _call, unsigned _sfi, unsigned _reg, ref<Expr> _value);
        void conOffObjectWrite(ObjectState* objectState, unsigned offset, ref<Expr> expr);
        void symOffObjectWrite(ObjectState* objectState);
    };
}

#endif


