#ifndef EXESTATEVEX_H
#define EXESTATEVEX_H

#include "klee/ExecutionState.h"

struct breadcrumb;

class Guest;

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
	uint64_t	last_syscall_inst;	/* based on state's total inst */

protected:
	ExeStateVex()
	: reg_mo(NULL)
	, syscall_c(0) {}

	ExeStateVex(KFunction *kf)
	: ExecutionState(kf)
	, reg_mo(NULL)
	, syscall_c(0) {}

	ExeStateVex(const std::vector<ref<Expr> > &assumptions)
	: ExecutionState(assumptions)
	, reg_mo(NULL)
	, syscall_c(0) {}

	ExeStateVex(const ExeStateVex& src);

	static Guest*	base_guest;
	static uint64_t base_stack;
public:
	virtual ExecutionState* copy(void) const { return copy(this); }
	virtual ExecutionState* copy(const ExecutionState* es) const
	{ return new ExeStateVex(*(static_cast<const ExeStateVex*>(es))); }

	virtual ~ExeStateVex() {}

	void setLastSyscallInst(void) { last_syscall_inst = totalInsts; }
	uint64_t getInstSinceSyscall(void) const
	{ return totalInsts - last_syscall_inst; }

	void recordBreadcrumb(const struct breadcrumb* );
	RecordLog::const_iterator crumbBegin(void) const { return bc_log.begin(); }
	RecordLog::const_iterator crumbEnd(void) const { return bc_log.end(); }

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
	static void setBaseGuest(Guest* gs) { base_guest = gs; }
	static void setBaseStack(uint64_t p) { base_stack = p; }

	virtual void getGDBRegs(
		std::vector<uint8_t>& v,
		std::vector<bool>& is_conc) const;

	virtual uint64_t getAddrPC(void) const;
	void setAddrPC(uint64_t addr);
	virtual unsigned getStackDepth(void) const;

	virtual void inheritControl(ExecutionState& es);

	void logXferRegisters();
	void logXferStack();
	void logXferMO(uint64_t log_obj_mo);
	void logXferObj(const ObjectState* os, int tag, unsigned off = 0);

	void updateGuestRegs();
};

}

#endif
