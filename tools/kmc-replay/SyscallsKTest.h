#ifndef SYSCALLS_KTEST_H
#define SYSCALLS_KTEST_H

#include <stdio.h>
#include "syscall/syscalls.h"

#define KREPLAY_NOTE	"[kmc-replay] "
#define KREPLAY_SC	"[kmc-sc] "

namespace klee
{
class Crumbs;
class KTestStream;
}

class FileReconstructor;

class SyscallsKTest : public Syscalls
{
friend class SyscallsKTestPT;
public:
	static SyscallsKTest* create(Guest*, klee::KTestStream*, klee::Crumbs*);
	virtual ~SyscallsKTest();
	virtual uint64_t apply(SyscallParams& sp);


	static bool copyInRegMemObj(Guest* gs, klee::KTestStream*);
protected:
	virtual bool copyInRegMemObj(void);
	virtual bool copyInMemObj(uint64_t user_addr, unsigned int sz);
	virtual void setRet(uint64_t r);

	SyscallsKTest(
		Guest* in_g,
		klee::KTestStream* kts,
		klee::Crumbs* in_crumbs);

	FileReconstructor	*file_recons;

	klee::KTestStream	*kts;
	klee::Crumbs		*crumbs;	// not owner
private:

	void badCopyBail(void);
	void feedSyscallOp(SyscallParams& sp);

	uint64_t getRet(void) const;

	int loadSyscallEntry(SyscallParams& sp);

	void sc_stat(SyscallParams& sp);
	void sc_mmap(SyscallParams& sp);

	unsigned int		sc_retired;
	struct bc_syscall	*bcs_crumb;
};

#endif
