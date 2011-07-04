#ifndef REPLAYEXEC_H
#define REPLAYEXEC_H

#include <stdint.h>
#include <stdio.h>
#include "vexexec.h"

class SyscallsKTest;

class ReplayExec : public VexExec
{
public:
	virtual ~ReplayExec();

	void setSyscallsKTest(SyscallsKTest* in_skt);
	void setRegLog(const char* reglog_fname);

	ReplayExec(Guest* gs, VexXlate* vx = NULL)
	: VexExec(gs, vx),
	  skt(NULL),
	  f_reglog(NULL)
	{ }

protected:
	virtual guest_ptr doVexSB(VexSB* sb);
	virtual void doSysCall(VexSB* sb);

private:
	uint8_t*	verifyWithRegLog(void);
	uint8_t*	feedRegLog(void);
	void		dumpRegBuf(const uint8_t*);

	SyscallsKTest	*skt;
	FILE		*f_reglog;
};

#endif
