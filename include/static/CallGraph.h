#ifndef _SLICER_UTIL_CALLGRAPH_H
#define	_SLICER_UTIL_CALLGRAPH_H

#include "llvm/Support/CFG.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Module.h"
#include "llvm/Function.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/InlineAsm.h"

#include "static/dsa/DataStructure.h"
#include "static/OrderedSet.h"
#include "static/Graph.h"
#include "static/Sugar.h"

#include <iostream>
#include <fstream>
#include <list>
#include <map>
#include <set>
#include <vector>

using namespace llvm;

namespace klee {

    class CallGraph : public GenericGraph<Function*> {
    public:

        CallGraph(Module* m, DataStructures *d) : module(m), dsa(d) {

            setupCalleeCallerMaps();
            setupMustHalt();
            setupPotentiallyHaltingFunctions();
        }

        bool transitivelyCalls(Instruction* inst, std::string targetfn) {
            if (CallInst * ci = dyn_cast<CallInst > (inst)) {
                std::list<CallInst*> worklist;
                std::set<CallInst*> visited;
                worklist.push_back(ci);

                while (!worklist.empty()) {
                    CallInst* ci = worklist.front();
                    worklist.pop_front();
                    visited.insert(ci);

                    foreach(calleeit, callees_begin(ci), callees_end(ci)) {
                        Function* callee = calleeit->second;

                        if (callee->getNameStr() == targetfn) {
                            return true;
                        }

                        foreach(instit, inst_begin(callee), inst_end(callee)) {
                            Instruction* inst = &*instit;
                            if (CallInst * calleeci = dyn_cast<CallInst > (inst)) {
                                if (visited.find(calleeci) == visited.end()) {
                                    worklist.push_back(calleeci);
                                    visited.insert(calleeci);
                                }
                            }
                        }
                    }
                }
            }

            return false;
        }

        bool isBaseMustHalt(Instruction* inst) {
            if (CallInst * ci = dyn_cast<CallInst > (inst)) {
                Value* calledValue = ci->getCalledValue();
                if (InlineAsm * iasm = dyn_cast<InlineAsm > (calledValue)) {
                    if (iasm->getAsmString() == "hlt") {
                        return true;
                    }
                    return false;
                }
            }

            if (CallInst * ci = dyn_cast<CallInst > (inst)) {


                if (isBaseMustHaltCallInst(ci)) {
                    return true;
                }
            }

            return false;
        }

        bool isBaseMustHaltCallInst(CallInst* ci) {

            bool hasAnyCallees = false;

            foreach(it, calleesMap.lower_bound(ci), calleesMap.upper_bound(ci)) {
                hasAnyCallees = true;
                Function* f = it->second;
                if (!isBaseMustHaltFunction(f)) {
                    return false;
                }
            }
            if (!hasAnyCallees) {
                return false;
            }

            return true;
        }

        bool isBaseMustHaltFunction(Function* f) {
            bool res = (f->isDeclaration() && (
                    //f->getNameStr() == "klee_silent_exit"
                    //|| f->getNameStr().find("__assert_fail") == 0
                    f->getNameStr() == "_exit"
                    //|| f->getNameStr() == "klee_report_error"
                    //|| f->getNameStr() == "klee_abort"
                    //|| f->getNameStr() == "abort"
                    //|| f->getNameStr() == "exit"
                    //        || f->doesNotReturn()
                    ));

            return res;
        }

        bool calls(CallInst* ci, Function* f) {
            std::map<CallInst*, Function*>::iterator iter;
            for (iter = calleesMap.lower_bound(ci); iter != calleesMap.upper_bound(ci); ++iter) {
                Function* callee = iter->second;
                if (callee == f) {
                    return true;
                }
            }
            return false;
        }

        bool mustHaltInstruction(Instruction* inst) {
            if (CallInst * ci = dyn_cast<CallInst > (inst)) {
                return mustHaltCallInst(ci);
            }

            return false;
        }

        bool mustHaltCallInst(CallInst* ci) {

            foreach(it, callees_begin(ci), callees_end(ci)) {
                Function* callee = it->second;
                if (!mustHaltFunction(callee)) {
                    return false;
                }
            }

            return callees_begin(ci) != callees_end(ci);
        }

        bool mustHaltFunction(Function* f) {
            return mustHaltSet.find(f) != mustHaltSet.end();
        }

        bool mayHalt(Function* f) {
            return mayHaltSet.find(f) != mayHaltSet.end();
        }

        bool mayHalt(CallInst * ci) {
            Function* callee = ci->getCalledFunction();
            if (callee && (callee->getNameStr() == "error" || callee->getNameStr() == "__error")) {
                Value* v = ci->getOperand(1);
                if (ConstantInt * c = dyn_cast<ConstantInt > (v)) {
                    if (!c->isZero()) {
                        return true;
                    }
                }
                return false;
            }

            foreach(it, calleesMap.lower_bound(ci), calleesMap.upper_bound(ci)) {
                Function* f = it->second;
                if (mayHaltSet.find(f) != mayHaltSet.end()) {
                    return true;
                }
            }

            return false;
        }

        std::set<Function*>::iterator mayHalt_begin() {
            return mayHaltSet.begin();
        }

        std::set<Function*>::iterator mayHalt_end() {
            return mayHaltSet.end();
        }

        std::multimap<CallInst*, Function*>::iterator callees_begin(CallInst* ci) {
            return calleesMap.lower_bound(ci);
        }

        std::multimap<CallInst*, Function*>::iterator callees_end(CallInst* ci) {
            return calleesMap.upper_bound(ci);
        }

        std::multimap<Function*, CallInst*>::iterator callers_begin(Function* f) {
            return callersMap.lower_bound(f);
        }

        std::multimap<Function*, CallInst*>::iterator callers_end(Function* f) {
            return callersMap.upper_bound(f);
        }

        void setupCalleeCallerMaps() {

            foreach(it, module->begin(), module->end()) {
                addNode(&*it);
            }

            foreach(fit, module->begin(), module->end()) {
                Function* caller = &*fit;

                foreach(iit, inst_begin(caller), inst_end(caller)) {
                    CallInst* ci = dyn_cast<CallInst > (&*iit);
                    if (!ci) continue;

                    if (Function * directCallee = ci->getCalledFunction()) {
                        addEdge(caller, directCallee);
                        calleesMap.insert(std::make_pair(ci, directCallee));
                        callersMap.insert(std::make_pair(directCallee, ci));
                    } else {

                        bool found = false;

                        foreach(ceit, dsa->callee_begin(ci), dsa->callee_end(ci)) {
                            found = true;
                            const Function * const callee1 = *ceit;
                            const Function* callee2 = const_cast<const Function*> (callee1);
                            Function* callee = const_cast<Function*> (callee2);
                            addEdge(caller, callee);
                            calleesMap.insert(std::make_pair(ci, callee));
                            callersMap.insert(std::make_pair(callee, ci));
                        }
                        if (!found) {
                            Value* calledValue = ci->getCalledValue();
                            if (!isa<InlineAsm > (calledValue)) {
                                std::cout << "NO CALLEES: " << ci->getParent()->getParent()->getNameStr();
                                std::cout << " " << ci->getParent()->getNameStr() << " " << *ci << std::endl;
                            }
                        }
                    }
                }
            }
        }

        static bool isTarget(CallInst* ci) {
            Function* callee = ci->getCalledFunction();
            if (!callee) return false;
            return callee->getNameStr() == "klee_slicer_target";
        }

        bool checkIfMustHalt(Function *f) {
            if (mustHaltFunction(f)) return true;
            if (f->isDeclaration()) return false;

            OrderedSet<BasicBlock*> worklist;
            std::set<BasicBlock*> musthaltbb;

            foreach(bbit, f->begin(), f->end()) {
                BasicBlock* bb = &*bbit;

                foreach(instit, bb->begin(), bb->end()) {
                    if (CallInst * ci = dyn_cast<CallInst > (&*instit)) {

                        if (mustHaltCallInst(ci)) {
                            if (f->getNameStr() == "open_temp") {
                                cout << "open_temp.bb.insert: " << bb->getNameStr() << " " << *ci << std::endl;
                            }
                            musthaltbb.insert(bb);

                            foreach(predit, pred_begin(bb), pred_end(bb)) {
                                BasicBlock* predbb = *predit;
                                worklist.add(predbb);
                            }
                            break;
                        }
                    }
                }
            }

            while (!worklist.empty()) {
                BasicBlock* bb = worklist.pop();

                /*if (f->getNameStr() == "_obstack_newchunk") {
                    cout << "bb: " << bb->getNameStr() << std::endl;

                    foreach(it, musthaltbb.begin(), musthaltbb.end()) {
                        cout << " musthalt: " << (*it)->getNameStr() << std::endl;
                    }
                }*/

                bool allsuccsmusthalt = true;

                foreach(succit, succ_begin(bb), succ_end(bb)) {
                    BasicBlock* succbb = *succit;
                    if (musthaltbb.find(succbb) == musthaltbb.end()) {
                        allsuccsmusthalt = false;
                        break;
                    }
                }

                if (allsuccsmusthalt) {
                    /*if (f->getNameStr() == "_obstack_newchunk") {
                        cout << " allsuccbb: " << bb->getNameStr() << std::endl;
                    }*/

                    if (musthaltbb.find(bb) == musthaltbb.end()) {
                        musthaltbb.insert(bb);

                        foreach(predit, pred_begin(bb), pred_end(bb)) {
                            BasicBlock* predbb = *predit;
                            worklist.add(predbb);
                        }
                    }
                }
            }

            if (f->getNameStr() == "open_temp") {

                foreach(it, musthaltbb.begin(), musthaltbb.end()) {
                    BasicBlock* bb = *it;
                    cout << "\topen_temp.abort.bb: " << bb->getNameStr() << " " << std::endl;
                }
            }

            BasicBlock* frontbb = &f->front();
            return (musthaltbb.find(frontbb) != musthaltbb.end());
        }

        bool hasTarget(Function* f) {

            foreach(instit, inst_begin(f), inst_end(f)) {
                Instruction* inst = &*instit;
                if (CallInst * ci = dyn_cast<CallInst > (inst)) {
                    if (isTarget(ci)) {
                        return true;
                    }
                }
            }
            return false;
        }

        void setupMustHalt() {
            OrderedSet<Function*> worklist;

            foreach(it, module->begin(), module->end()) {
                Function* f = &*it;
                if (isBaseMustHaltFunction(f) && !hasTarget(f)) {
                    mustHaltSet.insert(f);

                    foreach(it, callersMap.lower_bound(f), callersMap.upper_bound(f)) {
                        Function* caller = it->second->getParent()->getParent();
                        if (mustHaltSet.find(caller) == mustHaltSet.end()) {
                            worklist.add(caller);
                        }
                    }
                }
            }

            while (!worklist.empty()) {
                Function* w = worklist.pop();

                if (mustHaltSet.find(w) != mustHaltSet.end()) {
                    continue;
                }

                if (!hasTarget(w) && checkIfMustHalt(w)) {
                    mustHaltSet.insert(w);

                    foreach(it, callersMap.lower_bound(w), callersMap.upper_bound(w)) {
                        Function* caller = it->second->getParent()->getParent();
                        if (mustHaltSet.find(caller) == mustHaltSet.end()) {
                            worklist.add(caller);
                        }
                    }
                }
            }

            /*foreach(it, mustHaltSet.begin(), mustHaltSet.end()) {
                Function* f = *it;
                cout << "must halt: " << f->getNameStr() << std::endl;
            }*/
        }

        void setupPotentiallyHaltingFunctions() {
            std::list<Function*> worklist;

            foreach(it, module->begin(), module->end()) {
                Function* f = &*it;
                if (isBaseMustHaltFunction(f)) {
                    worklist.push_back(f);
                    std::cout << "MAY HALT 1: " << f->getNameStr() << std::endl;
                    mayHaltSet.insert(f);
                }
            }

            while (!worklist.empty()) {
                Function* w = worklist.front();
                worklist.pop_front();

                foreach(it, callersMap.lower_bound(w), callersMap.upper_bound(w)) {
                    CallInst* ci = it->second;

                    Function* callee = ci->getCalledFunction();
                    if (callee && (callee->getNameStr() == "__error")) {
                        Value* v = ci->getOperand(1);
                        if (ConstantInt * c = dyn_cast<ConstantInt > (v)) {
                            if (c->isZero()) {
                                continue;
                            }
                        }
                    }


                    Function* caller = it->second->getParent()->getParent();
                    if (caller->getNameStr() == "xalloc_die")
                        continue;
                    
                    if (mayHaltSet.find(caller) == mayHaltSet.end()) {
                        std::cout << "MAY HALT 2: " << caller->getNameStr() << " b/c " << w->getNameStr() << std::endl;
                        mayHaltSet.insert(caller);
                        worklist.push_back(caller);
                    }
                }
            }
        }

        Module* module;
        DataStructures* dsa;
        std::multimap<CallInst*, Function*> calleesMap;
        std::multimap<Function*, CallInst*> callersMap;
        std::set<Function*> mayHaltSet;
        std::set<Function*> mustHaltSet;
    };
}

#endif