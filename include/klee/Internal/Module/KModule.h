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
#include "klee/Internal/Module/KFunction.h"
#include <tr1/unordered_map>
#include <map>
#include <set>
#include <vector>

namespace llvm
{
	class Type;
	class BasicBlock;
	class Constant;
	class Function;
	class Instruction;
	class Module;
	class TargetData;
	class Value;
	class FunctionPassManager;
	class FunctionPass;
	class raw_os_ostream;
}

namespace klee
{
class Cell;
class Executor;
class Expr;
class InterpreterHandler;
class InstructionInfoTable;
class KInstruction;
class KModule;

template<class T> class ref;

class KConstant
{
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
class PhiCleanerPass;

class KModule
{
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
    KModule(llvm::Module *_module, const Interpreter::ModuleOptions &opts);
    virtual ~KModule();

    /// Initialize local data structures.
    //
    // FIXME: ihandler should not be here
    virtual void prepare(InterpreterHandler *ihandler);
    // link mod with object's module
    void addModule(llvm::Module* mod);

    /// Return an id for the given constant, creating a new one if necessary.
    unsigned getConstantID(llvm::Constant *c, KInstruction* ki);

    KFunction* addUntrackedFunction(llvm::Function* f);
    KFunction* addFunction(llvm::Function *f);
    KFunction* getKFunction(llvm::Function* f) const;
    KFunction* getKFunction(const char* name) const;

    std::vector<KFunction*>::const_iterator kfuncsBegin() const
    { return functions.begin(); }

    std::vector<KFunction*>::const_iterator kfuncsEnd() const
    { return functions.end(); }
    unsigned getNumKFuncs(void) const { return functions.size(); }

    unsigned getWidthForLLVMType(llvm::Type* type) const;

    KFunction* addFunctionProcessed(llvm::Function *f);

	void bindModuleConstTable(Executor* exe);
	void bindModuleConstants(Executor* exe);
	void bindInstructionConstants(Executor* exe, KInstruction *KI);
	void bindKFuncConstants(Executor* exe, KFunction* kf);

	const std::string& getLibraryDir(void) const { return opts.LibraryDir; }

	void addFunctionPass(llvm::FunctionPass* fp);
private:
	void setupFunctionPasses(void);
	void prepareMerge(InterpreterHandler *ih);
	void outputSource(InterpreterHandler* ih);
	void addMergeExit(llvm::Function* mergeFn, const std::string& name);
	void passEnforceInvariants(void);
	void injectRawChecks();
	void loadIntrinsicsLib();
	void dumpModule(void);
	void outputFunction(const KFunction* kf);

	void outputTruncSource(std::ostream* os, llvm::raw_os_ostream* ros) const;

	// Our shadow versions of LLVM structures.
	std::vector<KFunction*> functions;
	typedef std::tr1::unordered_map<llvm::Function*, KFunction*>
	func2kfunc_ty;
	func2kfunc_ty functionMap;

	llvm::FunctionPassManager	*fpm;
	InterpreterHandler		*ih;

	Interpreter::ModuleOptions opts;

	unsigned			updated_funcs;
};
} // End klee namespace

#endif
