#ifndef REPLAYEXEC_H
#define REPLAYEXEC_H

#include <stdint.h>
#include <stdio.h>
#include "vexexec.h"

class SyscallsKTest;

namespace klee
{
class Crumbs;
}

class ReplayExec : public VexExec
{
public:
	virtual ~ReplayExec();

	void setSyscallsKTest(SyscallsKTest* in_skt);
	void setCrumbs(klee::Crumbs* in_c);
	ReplayExec(Guest* gs, VexXlate* vx = NULL);

protected:
	virtual guest_ptr doVexSB(VexSB* sb);
	virtual void doSysCall(VexSB* sb);

private:
	struct regchk_t {
		uint8_t* sym_reg;
		uint8_t* guest_reg;
		uint8_t* sym_mask;
		unsigned reg_sz;
	};

	uint8_t*	verifyWithRegLog(void);
	uint8_t*	regChk(const struct regchk_t&);
	void		verifyOrPanic(void);
	void		dumpRegBuf(const uint8_t*);

	SyscallsKTest	*skt;		/* destroyed by superclass dtor */
	bool		has_reglog;
	bool		ign_reglog;
	klee::Crumbs	*crumbs;	/* not owner */
	bool		ignored_last;
	bool		skipped_vsys;
	bool		print_exec;
};

#endif
