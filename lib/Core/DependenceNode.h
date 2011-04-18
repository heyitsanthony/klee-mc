#ifndef _DEPENDENCENODE_H
#define	_DEPENDENCENODE_H

#include "llvm/Value.h"
#include "llvm/Instruction.h"
#include "llvm/Support/raw_ostream.h"

#include "klee/Expr.h"
#include <iostream>
#include "klee/Internal/Module/KModule.h"
#include "llvm/Function.h"
#include "MallocKeyOffset.h"
#include <map>
#include <set>
#include <bitset>
#include <list>
#include <iostream>

using namespace llvm;

namespace klee {

    class StaticRecord;
    class StateRecord;
    class SymOffArrayAlloc;
    class ConOffArrayAlloc;
    class StackWrite;
    class SymOffObjectWrite;
    class ConOffObjectWrite;
    class DependenceNode;


    class DependenceNode {
    public:

        static unsigned inleafcount;
        static unsigned count;

        size_t lochash;
        size_t valhash;

        DependenceNode(StateRecord* rec);
        ~DependenceNode();

        virtual SymOffArrayAlloc* toSymOffArrayAlloc() {
            return NULL;
        }

        virtual ConOffArrayAlloc* toConOffArrayAlloc() {
            return NULL;
        }

        virtual StackWrite* toStackWrite() {
            return NULL;
        }

        virtual SymOffObjectWrite* toSymOffObjectWrite() {
            return NULL;
        }

        virtual ConOffObjectWrite* toConOffObjectWrite() {
            return NULL;
        }

        void addPred(DependenceNode* p) {
            preds.push_back(p);
            p->succs.push_back(this);
        }

        virtual void print(std::ostream& out) const;
        friend std::ostream & operator<<(std::ostream &out, const DependenceNode &dn);

        StateRecord* rec;
        Instruction* inst;
        std::vector<DependenceNode*> preds;
        std::vector<DependenceNode*> succs;
        std::set<StateRecord*> appearsInLiveReads;
        std::set<StaticRecord*> sources;
    };

    class SymOffArrayAlloc : public DependenceNode {
    public:
        static unsigned count;
        MallocKey mallocKey;

        SymOffArrayAlloc(StateRecord* allocRec, const MallocKey& mk);

        SymOffArrayAlloc* toSymOffArrayAlloc() {
            return this;
        }
        void print(std::ostream& out) const;
    };
    
    class ConOffArrayAlloc : public DependenceNode {
    public:
        static unsigned count;
        MallocKeyOffset mallocKeyOffset;

        ConOffArrayAlloc(StateRecord* allocRec, const MallocKey& mk, unsigned offset);

        ConOffArrayAlloc* toConOffArrayAlloc() {
            return this;
        }
        void print(std::ostream& out) const;
    };

    class StackWrite : public DependenceNode {
    public:
        static unsigned count;

        StackWrite(StateRecord* rec, KFunction* _kf, unsigned _call, unsigned _sfi, unsigned _reg, ref<Expr> _value);

        StackWrite* toStackWrite() {
            return this;
        }
        void print(std::ostream& out) const;

        KFunction* kf;
        unsigned call;
        unsigned sfi;
        unsigned reg;
        ref<Expr> value;

        unsigned hash;
    };

    class SymOffObjectWrite : public DependenceNode {
    public:
        static unsigned count;

        SymOffObjectWrite(StateRecord* rec, ObjectState* _objectState);

        SymOffObjectWrite* toSymOffObjectWrite() {
            return this;
        }
        void print(std::ostream& out) const;
        ~SymOffObjectWrite();

        MallocKey mallocKey;
        ObjectState* objectStateCopy;
    };

    class ConOffObjectWrite : public DependenceNode {
    public:
        static unsigned count;

        ConOffObjectWrite(StateRecord* rec, ObjectState* objectState, unsigned offset, ref<Expr> value);

        ConOffObjectWrite* toConOffObjectWrite() {
            return this;
        }
        void print(std::ostream& out) const;

        MallocKey mallocKey;
        unsigned offset;
        ref<Expr> value;
    };

    struct DependenceNodeLessThan {
        bool operator() (DependenceNode* n1, DependenceNode * n2);
    };
}
#endif
