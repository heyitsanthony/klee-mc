#ifndef SYSCALLS_KTEST_H
#define SYSCALLS_KTEST_H

#include <stdio.h>
#include "syscall/syscalls.h"

#define SC_FL_NEWREGS	1

struct SCEntry
{
	uint64_t	sce_sysnr;
	uint64_t	sce_ret;
	uint64_t	sce_flags;
};

class SyscallsKTest : public Syscalls
{
public:
	static SyscallsKTest* create(Guest*, const char*, const char* sclog);
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
	bool copyInSCEntry(void);
	void setRet(uint64_t r);
	uint64_t getRet(void) const;

	void loadSyscallEntry(SyscallParams& sp);

	void sc_stat(SyscallParams& sp);
	void sc_mmap(SyscallParams& sp);
	void sc_munmap(SyscallParams& sp);

	struct KTest	*ktest;
	unsigned int	next_ktest_obj;

	unsigned int	sc_retired;

	FILE		*sclog;


	/* valid only for the duration of an apply */
	struct SCEntry	sce;
};

#endif
