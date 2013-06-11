#ifndef SYSCALLKTESTBC_H
#define SYSCALLKTESTBC_H

#include "ptimgremote.h"
#include "SyscallsKTest.h"

namespace klee
{
class KTestStream;

/* XXX I don't like this name */
class SyscallsKTestBC : public SyscallsKTest
{
public:
	KTestStream* getKTS(void) { return kts; }
	static SyscallsKTestBC* create(
		PTImgRemote	*gpt,
		KTestStream	*kts,
		const char	*test_path,
		unsigned	test_num);
	static KTestStream* createKTS(const char* dirname, unsigned test_num);

	virtual uint64_t apply(SyscallParams& sp)
	{ return SyscallsKTest::apply(sp); }
	virtual bool apply(void);
	virtual ~SyscallsKTestBC();
protected:
	SyscallsKTestBC(PTImgRemote* g, KTestStream* _kts, Crumbs* _crumbs)
	: SyscallsKTest(g, _kts, _crumbs)
	, pt_r(g)
	, seen_begin_file(false)
	, prologue_c(0)
	, epilogue_c(0)
	, skipped_c(0)
	, syscall_c(0) 
	, skipped_mem_c(0)
	, used_c(0) {}

	bool isReplayEnabled(SyscallParams& sp);

	struct bc_syscall* peekSyscallCrumb(void);
private:
	PTImgRemote	*pt_r;
	bool		seen_begin_file;/* whether log should replay */
	unsigned	prologue_c;	/* total syscalls prior to log */
	unsigned	epilogue_c;	/* total syscalls past log */
	unsigned	skipped_c;	/* total pass-through syscalls */
	unsigned	syscall_c;	/* total syscalls attempted */
	unsigned	skipped_mem_c;	/* mem syscalls skipped in log */
	unsigned	used_c;		/* syscalls used from log */
};

}
#endif
