#ifndef REPLAYEXEC_H
#define REPLAYEXEC_H

#include <stdint.h>
#include <stdio.h>
#include "vexexec.h"

class Syscalls;

namespace klee
{
class Crumbs;
}

class ReplayMismatch
{
public:
	virtual void print(std::ostream& os) = 0;
	virtual ~ReplayMismatch() {}
	Guest* getGuest(void) const { return gs; }
protected:
	ReplayMismatch(Guest *_gs) : gs(_gs) {}
	Guest	*gs;
};

class MultiReplayMismatch : public ReplayMismatch
{
public:
	static ReplayMismatch* create(const std::vector<ReplayMismatch*>& v);
	virtual void print(std::ostream& os);
	virtual ~MultiReplayMismatch();
protected:
	MultiReplayMismatch(const std::vector<ReplayMismatch*>& _m)
	: ReplayMismatch(_m[0]->getGuest())
	, m(_m) {}
private:
	std::vector<ReplayMismatch*>	m;
};

class MemReplayMismatch : public ReplayMismatch
{
public:
	MemReplayMismatch(Guest* _gs,
		void*	_base,
		uint8_t* _sym_obj,
		unsigned _len)
	: ReplayMismatch(_gs)
	, base(_base)
	, sym_obj(_sym_obj)
	, len(_len) {}
	virtual ~MemReplayMismatch() { delete [] sym_obj; }
	virtual void print(std::ostream& os);
private:
	void		*base;
	uint8_t		*sym_obj;
	unsigned	len;
};

class StackReplayMismatch : public ReplayMismatch
{
public:
	StackReplayMismatch(Guest* _gs, uint8_t* _sym_stk, unsigned _len)
	: ReplayMismatch(_gs)
	, sym_stk(_sym_stk)
	, len(_len) {}
	virtual ~StackReplayMismatch() { delete [] sym_stk; }
	virtual void print(std::ostream& os);
private:
	uint8_t		*sym_stk;
	unsigned	len;
};

class RegReplayMismatch : public ReplayMismatch
{
public:
	RegReplayMismatch(Guest* _gs, uint8_t* reg_m)
	: ReplayMismatch(_gs)
	, reg_mismatch(reg_m) { assert (reg_m != NULL); }

	virtual ~RegReplayMismatch(void) { delete [] reg_mismatch; }
	virtual void print(std::ostream& os);
private:
	uint8_t	*reg_mismatch;
};

class ReplayExec : public VexExec
{
public:
	virtual ~ReplayExec();

	void setCrumbs(klee::Crumbs* in_c);
	ReplayExec(Guest* gs, std::shared_ptr<VexXlate> vx = nullptr);

protected:
	virtual guest_ptr doVexSB(VexSB* sb);
	virtual void doTrap(VexSB* sb);
	virtual void doSysCall(VexSB* sb);

private:
	struct regchk_t {
		uint8_t* sym_reg;
		uint8_t* guest_reg;
		uint8_t* sym_mask;
		unsigned reg_sz;
	};

	unsigned	getCPUSize(void);
	ReplayMismatch*	verifyWithLog(void);
	ReplayMismatch*	doChks(struct breadcrumb* &bc);

	RegReplayMismatch*	regChk(const struct regchk_t&);
	StackReplayMismatch*	stackChk(const struct regchk_t&);
	MemReplayMismatch*	memChk(const struct regchk_t&);

	void		verifyOrPanic(VexSB* last_dispatched = NULL);
	void		dumpRegBuf(const uint8_t*);


	bool		has_reglog;
	bool		ign_reglog;
	klee::Crumbs	*crumbs;	/* not owner */
	bool		ignored_last;
	bool		skipped_vsys;
	bool		print_exec;
	bool		is_vdso_patched;

	unsigned	chklog_c;	/* number of basic blocks checked */
};

#endif
