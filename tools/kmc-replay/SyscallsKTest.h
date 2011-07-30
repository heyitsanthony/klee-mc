#ifndef SYSCALLS_KTEST_H
#define SYSCALLS_KTEST_H

#include <stdio.h>
#include "syscall/syscalls.h"

class Crumbs;

class SyscallsKTest : public Syscalls
{
public:
	static SyscallsKTest* create(Guest*, const char*, Crumbs*);
	virtual ~SyscallsKTest();
	virtual uint64_t apply(SyscallParams& sp);
private:
	SyscallsKTest(
		Guest* in_g,
		const char* fname_ktest,
		Crumbs* in_crumbs);

	char* feedMemObj(unsigned int sz);
	bool copyInRegMemObj(void);
	bool copyInMemObj(uint64_t user_addr, unsigned int sz);
	void setRet(uint64_t r);
	uint64_t getRet(void) const;

	void loadSyscallEntry(SyscallParams& sp);

	void sc_stat(SyscallParams& sp);
	void sc_mmap(SyscallParams& sp);
	void sc_munmap(SyscallParams& sp);

	struct KTest	*ktest;
	unsigned int	next_ktest_obj;

	unsigned int	sc_retired;

	Crumbs			*crumbs;	// not owner
	struct bc_syscall	*bcs_crumb;
};

#endif
