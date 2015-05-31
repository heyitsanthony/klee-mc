//===-- ExternalDispatcher.h ------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_EXTERNALDISPATCHER_H
#define KLEE_EXTERNALDISPATCHER_H

#include <map>
#include <string>
#include <stdint.h>
#include <memory>

namespace llvm {
	class ExecutionEngine;
	class Instruction;
	class Function;
	class FunctionType;
	class Module;
}


namespace klee {
class DispatcherMem;
class ExternalDispatcher
{
private:
	typedef void(*dispatch_jit_fptr_t)(void);
	typedef std::map<const llvm::Instruction*, llvm::Function*>
		dispatchers_ty;
	typedef std::map<llvm::Function*,dispatch_jit_fptr_t>
		dispatches_jitted_t;

	dispatchers_ty dispatchers;
	dispatches_jitted_t dispatches_jitted;
	
	std::unique_ptr<llvm::Module> dispatchModule;
	std::unique_ptr<llvm::ExecutionEngine> exeEngine;
	std::map<std::string, void*> preboundFunctions;
    
	llvm::Function *createDispatcherThunk(
		llvm::Function *f, llvm::Instruction *i);

	llvm::Function *findDispatchThunk(
		llvm::Function* f, llvm::Instruction *i);

	bool runProtectedCall(dispatch_jit_fptr_t f, uint64_t *args);
 
 	void buildExeEngine(void);

	std::unique_ptr<DispatcherMem>	smm;
public:
	ExternalDispatcher();
	~ExternalDispatcher();

	/**
	 * Call the given function using the parameter passing convention of
	 * ci with arguments in args[1], args[2], ... and writing the result
	 * into args[0].
	 */
	bool executeCall(	llvm::Function *function,
				llvm::Instruction *i,
				uint64_t *args);

	void* resolveSymbol(const std::string& name) const;
};
}

#endif
