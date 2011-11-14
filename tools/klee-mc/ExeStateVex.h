#ifndef EXESTATEVEX_H
#define EXESTATEVEX_H

#include "klee/ExecutionState.h"

struct breadcrumb;

namespace klee
{
class MemoryObject;

#define ExeStateVexBuilder DefaultExeStateBuilder<ExeStateVex>

typedef std::vector <std::vector<unsigned char> > RecordLog;
class ExeStateVex : public ExecutionState
{
friend class ExeStateVexBuilder;
private:
	ExeStateVex &operator=(const ExeStateVex&);

	RecordLog	bc_log;	/* list of uninterpreted breadcrumbs */
	MemoryObject	*reg_mo;
	unsigned int	syscall_c;

protected:
	ExeStateVex() {}
  	ExeStateVex(KFunction *kf) : ExecutionState(kf) {}
	ExeStateVex(const std::vector<ref<Expr> > &assumptions)
	: ExecutionState(assumptions) {}
	ExeStateVex(const ExeStateVex& src);

public:
	virtual ExecutionState* copy(void) const { return copy(this); }
	virtual ExecutionState* copy(const ExecutionState* es) const
	{ return new ExeStateVex(*(static_cast<const ExeStateVex*>(es))); }

	virtual ~ExeStateVex() {}

	void recordBreadcrumb(const struct breadcrumb* );
	RecordLog::const_iterator crumbBegin(void) const { return bc_log.begin(); }
	RecordLog::const_iterator crumbEnd(void) const { return bc_log.end(); }

	void recordRegisters(const void* regs, int sz);

	MemoryObject* setRegCtx(MemoryObject* mo)
	{
		MemoryObject	*old_mo;
		old_mo = reg_mo;
		reg_mo = mo;
		return old_mo;
	}

	const MemoryObject* getRegCtx(void) const { return reg_mo; }
	ObjectState* getRegObj(void);
	const ObjectState* getRegObjRO(void) const;

	void incSyscallCount(void) { syscall_c++; }
	unsigned int getSyscallCount(void) const { return syscall_c; }
};

}

#endif
