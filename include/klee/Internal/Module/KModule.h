//===-- KModule.h -----------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_KMODULE_H
#define KLEE_KMODULE_H

#include "klee/Interpreter.h"

#include <map>
#include <set>
#include <vector>

namespace llvm {
  class BasicBlock;
  class Constant;
  class Function;
  class Instruction;
  class Module;
  class TargetData;
  class Value;
  class FunctionPassManager;
}

namespace klee {
  class Cell;
  class Executor;
  class Expr;
  class InterpreterHandler;
  class InstructionInfoTable;
  class KInstruction;
  class KModule;
  template<class T> class ref;

  struct KFunction {
    llvm::Function *function;

    unsigned numArgs, numRegisters;

    unsigned numInstructions;
    unsigned callcount;
    KInstruction **instructions;
    llvm::Value **arguments;

    std::map<llvm::BasicBlock*, unsigned> basicBlockEntry;

    /// Whether instructions in this function should count as
    /// "coverable" for statistics and search heuristics.
    bool trackCoverage;

  private:
    KFunction(const KFunction&);
    KFunction &operator=(const KFunction&);
    void addInstruction(
      KModule* km,
      llvm::Instruction* inst,
      std::map<llvm::Instruction*, unsigned>& registerMap,
      unsigned int& i);
  public:
    explicit KFunction(llvm::Function*, KModule *);
    ~KFunction();

    unsigned getArgRegister(unsigned index) { return index; }
    llvm::Value* getValueForRegister(unsigned reg);
  };


  class KConstant {
  public:
    /// Actual LLVM constant this represents.
    llvm::Constant* ct;

    /// The constant ID.
    unsigned id;

    /// First instruction where this constant was encountered, or NULL
    /// if not applicable/unavailable.
    KInstruction *ki;

    KConstant(llvm::Constant*, unsigned, KInstruction*);
  };


  class RaiseAsmPass;
  class IntrinsicCleanerPass;
  class DivCheckPass;
  class PhiCleanerPass;

  class KModule {
  public:
    llvm::Module *module;
    llvm::TargetData *targetData;

    // Some useful functions to know the address of
    llvm::Function *dbgStopPointFn, *kleeMergeFn;

    // Functions which escape (may be called indirectly)
    // XXX change to KFunction
    std::set<llvm::Function*> escapingFunctions;

    InstructionInfoTable *infos;

    std::vector<llvm::Constant*> constants;
    std::map<llvm::Constant*, KConstant*> constantMap;
    KConstant* getKConstant(llvm::Constant *c);


    std::vector<Cell>	constantTable;

  public:
    KModule(llvm::Module *_module);
    ~KModule();

    /// Initialize local data structures.
    //
    // FIXME: ihandler should not be here
    void prepare(const Interpreter::ModuleOptions &opts,
                 InterpreterHandler *ihandler);
    // link mod with object's module
    void addModule(llvm::Module* mod);

    /// Return an id for the given constant, creating a new one if necessary.
    unsigned getConstantID(llvm::Constant *c, KInstruction* ki);

    KFunction* addFunction(llvm::Function *f);
    KFunction* getKFunction(llvm::Function* f) const;
    KFunction* getKFunction(const char* name) const;

    std::vector<KFunction*>::const_iterator kfuncsBegin() const
    { return functions.begin(); }

    std::vector<KFunction*>::const_iterator kfuncsEnd() const
    { return functions.end(); }


    void bindModuleConstTable(Executor* exe);
  private:
	KFunction* addFunctionProcessed(llvm::Function *f);
	void prepareMerge(
		const Interpreter::ModuleOptions &opts,
		InterpreterHandler *ih);
	void outputSource(InterpreterHandler* ih);
	void addMergeExit(llvm::Function* mergeFn, const std::string& name);
	void passEnforceInvariants(void);
	void injectRawChecks(const Interpreter::ModuleOptions &opts);
	void loadIntrinsicsLib(const Interpreter::ModuleOptions &opts);

    // Our shadow versions of LLVM structures.
    std::vector<KFunction*> functions;
    std::map<llvm::Function*, KFunction*> functionMap;

    llvm::FunctionPassManager* fpm;
  };
} // End klee namespace

#endif
