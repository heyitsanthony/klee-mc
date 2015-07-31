#ifndef SYSCALLSKTESTPT_H
#define SYSCALLSKTESTPT_H

#include "SyscallsKTest.h"

class GuestPTMem;

class SyscallsKTestPT : public SyscallsKTest
{
public:
	virtual ~SyscallsKTestPT(void);
	SyscallsKTestPT(const SyscallsKTest* sckt);
	uint64_t apply(SyscallParams& sp) override;
protected:
	bool copyInRegMemObj(void) override;
	void setRet(uint64_t r) override;

private:
	bool			ret_set;
	GuestPTMem		*pt_mem;
};

#endif
