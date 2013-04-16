#ifndef SYSCALLSKTESTPT_H
#define SYSCALLSKTESTPT_H

#include "SyscallsKTest.h"

class GuestPTMem;

class SyscallsKTestPT : public SyscallsKTest
{
public:
	virtual ~SyscallsKTestPT(void);
	SyscallsKTestPT(const SyscallsKTest* sckt);
	virtual uint64_t apply(SyscallParams& sp);
protected:
	virtual bool copyInRegMemObj(void);
	virtual void setRet(uint64_t r);

private:
	bool			ret_set;
	GuestPTMem		*pt_mem;
};

#endif
