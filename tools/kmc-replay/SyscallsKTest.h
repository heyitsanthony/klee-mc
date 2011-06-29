#ifndef SYSCALLS_KTEST_H
#define SYSCALLS_KTEST_H

#include <stdio.h>
#include "syscall/syscalls.h"

class SyscallsKTest : public Syscalls
{
public:
	static SyscallsKTest* create(Guest*, const char*, const char*);
	virtual ~SyscallsKTest();
	virtual uint64_t apply(SyscallParams& sp);
private:
	SyscallsKTest(
		Guest* in_g, 
		const char* fname_ktest,
		const char* fname_sclog);

	char* feedMemObj(unsigned int sz);
	bool copyInRegMemObj(void);
	bool copyInMemObj(uint64_t user_addr, unsigned int sz);
	bool verifyCPURegs(void);
	char* feedCPURegs(void);

	void sc_stat(SyscallParams& sp);

	struct KTest	*ktest;
	unsigned int	next_ktest_obj;

	FILE		*f_sclog;
	unsigned int	sc_retired;
};

#endif