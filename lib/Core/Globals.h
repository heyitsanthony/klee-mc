#ifndef KLEE_GLOBALS_H
#define KLEE_GLOBALS_H

#include "klee/Expr.h"
#include <map>
#include <tr1/unordered_set>

namespace llvm
{
class GlobalValue;
class GlobalVariable;
class Constant;
class Module;
}

namespace klee
{
class KModule;
class ExternalDispatcher;
class MemoryObject;
class ConstantExpr;
class Executor;
class ExecutionState;

/* Manages global memory objects and addresses */
class Globals
{
public:
	typedef std::map<const llvm::GlobalValue*, ref<klee::ConstantExpr> >
		globaladdr_map;
	typedef std::map<const llvm::GlobalValue*, MemoryObject*>
		globalobj_map;

	Globals(
		const KModule* km,
		ExecutionState* init_state,
		const ExternalDispatcher*);
	virtual ~Globals() {}

	void allocGlobalVariableDecl(const llvm::GlobalVariable& gv);
	void allocGlobalVariableNoDecl(const llvm::GlobalVariable& gv);

	MemoryObject* findObject(const llvm::GlobalValue* gv) const;
	MemoryObject* findObject(const char* name) const;
	ref<ConstantExpr> findAddress(const char* gv) const;
	ref<ConstantExpr> findAddress(const llvm::GlobalValue* gv) const;

	bool isLegalFunction(uint64_t v) const
	{ return (legalFunctions.count(v) != 0); }

	auto beginAddrs(void) const { return globalAddresses.begin(); }
	auto endAddrs(void) const { return globalAddresses.end(); }

	void updateModule(void);
private:
	Globals() {}
	
	void setupCTypes(void);
	void setupFuncAddrs(llvm::Module* m);
	void setupGlobalObjects(llvm::Module* m);
	void setupAliases(llvm::Module* m);
	void setupGlobalData(llvm::Module* m);

	// Given a concrete object in our [klee's] address space, add it to
	// objects checked code can reference.
	MemoryObject *addExternalObject(
		void *addr,
		unsigned size,
		bool isReadOnly);

	void initializeGlobalObject(
		ObjectState *os,
		llvm::Constant *c,
		unsigned offset);

	const KModule			*kmodule;
	ExecutionState			*init_state;
	const ExternalDispatcher	*ed;

	/// The set of legal function addresses, used to validate function
	/// pointers. We use the actual Function* address as the function address.
	mutable std::tr1::unordered_set<uint64_t> legalFunctions;
	/// Map of globals to their bound address. This also includes
	/// globals that have no representative object (i.e. functions).
	mutable globaladdr_map globalAddresses;

	/// Map of globals to their representative memory object.
	globalobj_map globalObjects;
};
}

#endif
