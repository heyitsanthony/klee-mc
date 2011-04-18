#ifndef _ALIASINGMANAGER_H
#define	_ALIASINGMANAGER_H

#include "llvm/Module.h"
#include "llvm/LLVMContext.h"
#include "llvm/Bitcode/ReaderWriter.h"

#include "llvm/Analysis/Passes.h"
#include "llvm/PassManager.h"
#include "llvm/Linker.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/IPO.h"

#include "static/CallGraph.h"
#include "static/dsa/DSGraph.h"
#include "static/dsa/DataStructure.h"

using namespace llvm;

namespace klee {

    class AliasingRunner {
    public:
        Module* module;
        EQTDDataStructures* dsa;
        klee::CallGraph* callgraph;
        PassManager* passes;
        Pass* intern;

        AliasingRunner(Module* m) : module(m), dsa(0), callgraph(0) {
            passes = new PassManager();
            intern = createInternalizePass(true);
            passes->add(intern);
            passes->add(new TargetData(module));
            dsa = new EQTDDataStructures();
            passes->add(dsa);
            passes->run(*module);

            callgraph = new klee::CallGraph(module, dsa);

            int counter = 0;

            foreach(it, module->begin(), module->end()) {
                Function* f = &*it;
                if (!dsa->hasDSGraph(*f)) {
                    continue;
                }

                DSGraph* graph = dsa->getDSGraph(*f);
                if (graph) {

                    foreach(nodeit, graph->node_begin(), graph->node_end()) {
                        DSNode* node = &*nodeit;
                        if (node->name == -1) {
                            node->name = counter++;
                        }
                    }
                }
            }

            DSGraph* graph = dsa->getGlobalsGraph();
            if (graph) {

                foreach(nodeit, graph->node_begin(), graph->node_end()) {
                    DSNode* node = &*nodeit;
                    if (node->name == -1) {
                        node->name = counter++;
                    }
                }
            }
        }

    };
}
#endif	/* _ALIASINGMANAGER_H */

