#ifndef KLEE_GLOBALS_H
#define KLEE_GLOBALS_H

#include <set>

namespace llvm
{
class GlobalValue;
class GlobalVariable;
class Constant;
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
	Globals(
		const KModule* km,
		ExecutionState* init_state,
		const ExternalDispatcher*);
	virtual ~Globals() {}

	void allocGlobalVariableDecl(const llvm::GlobalVariable& gv);

	void allocGlobalVariableNoDecl(const llvm::GlobalVariable& gv);

	MemoryObject* findObject(const llvm::GlobalValue* gv) const;
	ref<ConstantExpr> findAddress(const llvm::GlobalValue* gv) const;

	bool isLegalFunction(uint64_t v) const
	{ return (legalFunctions.count(v) != 0); }

private:
	Globals() {}

	// Given a concrete object in our [klee's] address space, add it to
	// objects checked code can reference.
	MemoryObject *addExternalObject(
		void *addr,
		unsigned size,
		bool isReadOnly);

	void initializeGlobalObject(
		ObjectState *os, llvm::Constant *c, unsigned offset);

	const KModule			*kmodule;
	ExecutionState			*init_state;
	const ExternalDispatcher	*ed;

	typedef std::map<const llvm::GlobalValue*, ref<klee::ConstantExpr> >
		globaladdr_map;
	typedef std::map<const llvm::GlobalValue*, MemoryObject*>
		globalobj_map;

	/// The set of legal function addresses, used to validate function
	/// pointers. We use the actual Function* address as the function address.
	std::set<uint64_t> legalFunctions;

	/// Map of globals to their representative memory object.
	globalobj_map globalObjects;
	/// Map of globals to their bound address. This also includes
	/// globals that have no representative object (i.e. functions).
	globaladdr_map globalAddresses;


};
}

#endif
