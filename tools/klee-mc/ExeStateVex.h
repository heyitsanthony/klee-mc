#ifndef EXESTATEVEX_H
#define EXESTATEVEX_H

#include "klee/ExecutionState.h"

namespace klee
{

typedef std::vector <std::vector<unsigned char> > RegLog;
class ExeStateVex : public ExecutionState
{
private:
	ExeStateVex &operator=(const ExeStateVex&);

	RegLog		reg_log;
	RegLog		sc_log;
	MemoryObject	*reg_mo;

protected:
	ExeStateVex() {}
  	ExeStateVex(KFunction *kf) : ExecutionState(kf) {}
	ExeStateVex(const std::vector<ref<Expr> > &assumptions)
	: ExecutionState(assumptions) {}
	ExeStateVex(const ExeStateVex& src);

	virtual ExecutionState* create(void) const { return new ExeStateVex(); }
	virtual ExecutionState* create(KFunction* kf) const
	{ return new ExeStateVex(kf); }
	virtual ExecutionState* create(
		const std::vector<ref<Expr> >& assumptions) const
	{ return new ExeStateVex(assumptions); }

public:
	static ExeStateVex* make(KFunction* kf)
	{ return new ExeStateVex(kf); }
	static ExeStateVex* make(const std::vector<ref<Expr> >& assumptions)
	{ return new ExeStateVex(assumptions); }


	virtual ExecutionState* copy(void) const { return copy(this); }
	virtual ExecutionState* copy(const ExecutionState* es) const
	{ return new ExeStateVex(*(static_cast<const ExeStateVex*>(es))); }

	virtual ~ExeStateVex() {}

	void recordRegisters(const void* regs, int sz);
	RegLog::const_iterator regsBegin(void) const { return reg_log.begin(); }
	RegLog::const_iterator regsEnd(void) const { return reg_log.end(); }

	void recordSyscall(uint64_t sysnr, uint64_t ret, uint64_t flags);
	RegLog::const_iterator scBegin(void) const { return sc_log.begin(); }
	RegLog::const_iterator scEnd(void) const { return sc_log.end(); }


	MemoryObject* setRegCtx(MemoryObject* mo)
	{
		MemoryObject	*old_mo;
		old_mo = reg_mo;
		reg_mo = mo;
		return old_mo;
	}

	MemoryObject* getRegCtx(void) const { return reg_mo; }
};
}

#endif
