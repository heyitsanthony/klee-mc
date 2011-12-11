#ifndef SYSCALLS_KTEST_H
#define SYSCALLS_KTEST_H

#include <stdio.h>
#include "syscall/syscalls.h"

namespace klee
{
class Crumbs;
class KTestStream;
}

class SyscallsKTest : public Syscalls
{
public:
	static SyscallsKTest* create(Guest*, klee::KTestStream*, klee::Crumbs*);
	virtual ~SyscallsKTest();
	virtual uint64_t apply(SyscallParams& sp);
private:
	SyscallsKTest(
		Guest* in_g,
		klee::KTestStream* ,
		klee::Crumbs* in_crumbs);

	void badCopyBail(void);
	void feedSyscallOp(SyscallParams& sp);

	bool copyInRegMemObj(void);
	bool copyInMemObj(uint64_t user_addr, unsigned int sz);
	void setRet(uint64_t r);
	uint64_t getRet(void) const;

	int loadSyscallEntry(SyscallParams& sp);

	void sc_stat(SyscallParams& sp);
	void sc_mmap(SyscallParams& sp);

	klee::KTestStream	*kts;
	unsigned int		sc_retired;

	klee::Crumbs		*crumbs;	// not owner
	struct bc_syscall	*bcs_crumb;
};

#endif
