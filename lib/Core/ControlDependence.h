#ifndef _CONTROLDEPENDENCE_H
#define	_CONTROLDEPENDENCE_H

#include <map>
#include <set>
#include <list>
#include <fstream>
#include <vector>


namespace llvm {
    class Function;
    class CallInst;
    class ReturnInst;
    class Function;
    class Module;
}


using namespace llvm;

namespace klee {
    class AliasingRunner;
    class StaticRecord;
    class CallGraph;
    class ExitCDNode;
    class EntryCDNode;
    class SuperExitCDNode;
    class StaticRecordManager;
    class ReturnCDNode;
    class ReturnPredCDNode;
    class StaticRecordCDNode;
    class StartCDNode;    

    class CDNode {
    public:        
        std::set<CDNode*> icfg_succs;
        std::set<CDNode*> icfg_preds;

        std::set<CDNode*> pcg_preds;
        std::set<CDNode*> pcg_succs;

        std::set<CDNode*> acfg_preds;
        std::set<CDNode*> acfg_succs;

        std::set<CDNode*> acdg_preds;
        std::set<CDNode*> acdg_succs;

        std::set<CDNode*> postDomChildren;
        CDNode* idom;

        CDNode();

        virtual std::string name();

        virtual void print();

        virtual bool isPlaceHolder();

        virtual bool isACFGNode();

        virtual bool isACDGNode();

        void addICFGSucc(CDNode* n);

        void addPCGSucc(CDNode* n);

        void addACFGSucc(CDNode* n);

        void addACDGSucc(CDNode* n);

        virtual CallInst* toCallInst();

        virtual ReturnInst* toReturnInst();

        virtual EntryCDNode* toEntry();

        virtual ExitCDNode* toExit();

        virtual SuperExitCDNode* toSuperExit();

        virtual ReturnCDNode* toReturn();

        virtual StartCDNode* toStart();

        virtual StaticRecordCDNode* toStaticRecord();

        virtual ReturnPredCDNode* toReturnPred();
    };

    class ExitCDNode : public CDNode {
    public:
        Function* function;

        ExitCDNode(Function* _function);

        std::string name();

        ExitCDNode* toExit();

        void print();

        bool isACFGNode();

        bool isACDGNode();
    };

    class EntryCDNode : public CDNode {
    public:
        Function* function;

        EntryCDNode(Function* _function);

        void print();

        std::string name();

        EntryCDNode* toEntry();
        

        bool isPlaceHolder();

        bool isACFGNode();

        bool isACDGNode();
    };

    class SuperExitCDNode : public CDNode {
    public:

        void print();

        std::string name();

        SuperExitCDNode* toSuperExit();

        bool isACFGNode();

        bool isACDGNode();
    };

    class ReturnCDNode : public CDNode {
    public:
        CallInst* callinst;

        void print() ;

        std::string name();

        ReturnCDNode(CallInst* ci);
        ReturnCDNode* toReturn();

        bool isPlaceHolder();

        bool isACFGNode();

        bool isACDGNode();
    };

    class ReturnPredCDNode : public CDNode {
    public:
        CallInst* callinst;

        void print();
        std::string name();

        ReturnPredCDNode(CallInst* ci);

        ReturnPredCDNode* toReturnPred();

        bool isACFGNode();

        bool isACDGNode();
    };

    class StartCDNode : public CDNode {
    public:

        void print();

        std::string name();

        StartCDNode* toStart();

        bool isACFGNode();

        bool isACDGNode();
    };

    class StaticRecordCDNode : public CDNode {
    public:
        StaticRecord* staticRecord;

        std::string name();

        void print();

        StaticRecordCDNode(StaticRecord* _staticRecord);

        bool isACFGNode();

        bool isACDGNode();

        StaticRecordCDNode* toStaticRecord();

        bool isPredicate();

        CallInst* toCallInst();

        ReturnInst* toReturnInst();
    };

    class ACFG {
    public:
        std::map<StaticRecord*, StaticRecordCDNode*> regnodes;
        std::map<unsigned, StaticRecordCDNode*> phinodes;
        std::vector<StaticRecordCDNode*> srns;
        llvm::Function* function;
        std::map<CallInst*, ReturnCDNode*> retnodes;
        std::map<CallInst*, ReturnPredCDNode*> retprednodes;
        EntryCDNode* entry;
        ExitCDNode* exit;
        StartCDNode* start;
        SuperExitCDNode* superExit;

        std::set<CDNode*> nodes;
        StaticRecordManager* recm;
        CallGraph* callgraph;

        ACFG(llvm::Function* _function, StaticRecordManager* _recm, CallGraph* _callgraph);

        void san(std::string& s);

        void writeACDGGraph();

        void writeACFGGraph();

        ReturnCDNode* getReturnNode(CallInst* ci);

        void addSuccsOfTo(StaticRecord* of, CDNode* to);

        void buildACFG();

        void buildBGLACFG();

        void runPostDom();

        void constructControlEdges();

        void checkRep();
    };

    class ControlDependence {
    public:
        std::map<Function*, ACFG*> acfgs;
        Module* module;
        StaticRecordManager* recm;
        AliasingRunner* aliasingRunner;

        ControlDependence(Module* _module, StaticRecordManager* _recm) ;
        void propagateControlsExit();

        void buildACDG();

        void findControls();
    };
}
#endif	

