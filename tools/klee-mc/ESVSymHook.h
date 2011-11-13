#ifndef EXESTATESYMHOOK_H
#define EXESTATESYMHOOK_H

#include "ExeStateVex.h"

struct breadcrumb;

namespace llvm
{
class Function;
}

namespace klee
{
class MemoryObject;

#define ESVSymHookBuilder DefaultExeStateBuilder<ESVSymHook>

class ESVSymHook : public ExeStateVex
{
friend class ESVSymHookBuilder;
private:
	ESVSymHook &operator=(const ESVSymHook&);

protected:
	ESVSymHook() : cur_watched_f(0) {}
  	ESVSymHook(KFunction *kf)
	: ExeStateVex(kf), cur_watched_f(0) {}
	ESVSymHook(const std::vector<ref<Expr> > &assumptions)
	: ExeStateVex(assumptions), cur_watched_f(0) {}
	ESVSymHook(const ESVSymHook& src);

public:
	virtual ExecutionState* copy(void) const { return copy(this); }
	virtual ExecutionState* copy(const ExecutionState* es) const
	{ return new ESVSymHook(*(static_cast<const ESVSymHook*>(es))); }

	virtual ~ESVSymHook() {}

	llvm::Function* getWatchedFunc(void) const { return cur_watched_f; }
	bool isWatched(void) const { return cur_watched_f; }
	void setWatchedFunc(llvm::Function* f, ref<Expr> param);
	void unwatch(void) { cur_watched_f = NULL; }
	const ref<Expr>& getWatchParam(void) const { return param; }
	void incWatchDepth(void) { watched_depth++; }
	unsigned int decWatchDepth(void)
	{ watched_depth--; return watched_depth; }

	virtual void bindObject(const MemoryObject *mo, ObjectState *os);
	virtual void unbindObject(const MemoryObject* mo);


private:
	/* need two extra address spaces:
	 * 'blessed' mmap objects-- MO's that were created outside malloc()
	 * 'heap' objects -- ranges allocated by malloc()
	 *
	 * Inside malloc/free-- may access everything
	 * Outside malloc/free -- may only access blessed or heap
	 */

	llvm::Function*	cur_watched_f;
	ref<Expr>	param;
	unsigned int	watched_depth;
};

}

#endif
