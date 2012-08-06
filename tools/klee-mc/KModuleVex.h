#ifndef KMODULEVEX_H
#define KMODULEVEX_H

#include "DynGraph.h"
#include "klee/Internal/Module/KModule.h"
#include <tr1/unordered_map>

class VexXlate;
class VexSB;
class VexFCache;
class Guest;

namespace llvm
{
class Function;
}

namespace klee
{
class Executor;
class KModuleVex : public KModule
{
typedef std::tr1::unordered_map<uintptr_t /* Func*/, VexSB*> func2vsb_map;

public:
	KModuleVex(
		Executor* _exe,
		Interpreter::ModuleOptions& mod_opts,
		Guest* _gs);
	virtual ~KModuleVex(void);

	llvm::Function* getFuncByAddrNoKMod(uint64_t guest_addr, bool& is_new);
	llvm::Function* getFuncByAddr(uint64_t guest_addr);
	const VexSB* getVSB(llvm::Function* f) const;

	VexXlate* getXlate(void) const { return xlate; }

	virtual void prepare(InterpreterHandler *ihandler);
private:
	void scanFuncExits(uint64_t guest_addr, llvm::Function* f);
	void writeCodeGraph(GenericGraph<guest_ptr>& g);

	Executor	*exe;
	Guest		*gs;

	func2vsb_map	func2vsb_table;
	VexFCache	*xlate_cache;
	VexXlate	*xlate;

	DynGraph	ctrl_graph;

	unsigned int	native_code_bytes;
	bool		in_scan;
};
}

#endif
