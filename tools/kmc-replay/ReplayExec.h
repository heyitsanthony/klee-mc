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
	uint8_t*	verifyWithRegLog(void);
	void		verifyOrPanic(void);
	void		dumpRegBuf(const uint8_t*);

	SyscallsKTest	*skt;		/* destroyed by superclass dtor */
	bool		has_reglog;
	bool		ign_reglog;
	klee::Crumbs	*crumbs;	/* not owner */
	bool		ignored_last;
	bool		skipped_vsys;
};

#endif
