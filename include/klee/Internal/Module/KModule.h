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
#include <unordered_map>
#include <memory>
#include <string>
#include <map>
#include <set>
#include "static/Sugar.h"

namespace llvm
{
	class Type;
	class BasicBlock;
	class Constant;
	class Function;
	class Instruction;
	class Module;
	class DataLayout;
	class Value;
	class FunctionPass;
	class raw_os_ostream;
	namespace legacy {
		class FunctionPassManager;
	}
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
	std::shared_ptr<llvm::Module> module;
	std::unique_ptr<llvm::DataLayout> dataLayout;

    // Some useful functions to know the address of
    llvm::Function *dbgStopPointFn, *kleeMergeFn;

    // Functions which escape (may be called indirectly)
    // XXX change to KFunction
    std::set<llvm::Function*> escapingFunctions;

    std::unique_ptr<InstructionInfoTable> infos;

    std::vector<llvm::Constant*> constants;
    std::unordered_map<llvm::Constant*, std::unique_ptr<KConstant>> constantMap;
    KConstant* getKConstant(llvm::Constant *c);

    std::vector<Cell>	constantTable;

public:
    KModule(llvm::Module *_module, const ModuleOptions &opts);
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
    virtual KFunction* addFunction(llvm::Function *f);
    KFunction* getKFunction(const llvm::Function* f) const;
    KFunction* getKFunction(const char* name) const;

	KFunction* buildListFunc(
		const std::vector<llvm::Function*>& kfs,
		const char* name);

    ptr_vec_t<KFunction>::const_iterator kfuncsBegin() const
    { return functions.begin(); }
    ptr_vec_t<KFunction>::const_iterator kfuncsEnd() const
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
	void dumpFuncs(std::ostream& os) const;

	void setPrettyName(const llvm::Function* f, const std::string& s);
	std::string getPrettyName(const llvm::Function* f) const;
	const KFunction* getPrettyFunc(const char* s) const;

	void setModName(KFunction* kf, const char* modname);

	/* init/fini handling */
	void addFiniFunction(KFunction* kf) { fini_funcs.insert(kf); }
	void addInitFunction(KFunction* kf) { init_funcs.insert(kf); }

	KFunction* getFiniFunc(void) const { return fini_kfunc; }
	KFunction* getInitFunc(void) const { return init_kfunc; }
	void setupFiniFuncs(void);
	void setupInitFuncs(void);

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

	bool outputTruncSource(
		std::ostream* os, llvm::raw_os_ostream* ros) const;

	// Our shadow versions of LLVM structures.
	ptr_vec_t<KFunction>	functions;
	typedef std::unordered_map<const llvm::Function*, KFunction*>
		func2kfunc_ty;
	func2kfunc_ty functionMap;

	std::unique_ptr<llvm::legacy::FunctionPassManager> fpm;
	InterpreterHandler			*ih;

	ModuleOptions			opts;

	unsigned			updated_funcs;
	std::set<std::string>		addedModules;
	typedef std::map<std::string, std::string*> modname_ty;
	modname_ty	modNames;

	typedef std::unordered_map<const llvm::Function*, std::string>
		f2pretty_t;
	typedef std::unordered_map<std::string, const KFunction*>
		pretty2f_t;
	f2pretty_t	prettyNames;
	pretty2f_t	prettyFuncs;

	std::set<KFunction*>	init_funcs;
	KFunction		*init_kfunc;

	std::set<KFunction*>	fini_funcs;
	KFunction		*fini_kfunc;
};
} // End klee namespace

#endif
