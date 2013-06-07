/* a system call replayer which uses the bitcode library from klee */
#ifndef SYSCALLSMODEL_H
#define SYSCALLSMODEL_H

#include <stdio.h>
#include <setjmp.h>
#include "guestmem.h"
#include "syscall/syscalls.h"

class FileReconstructor;

namespace llvm { class ExecutionEngine; }
namespace klee { class KTestStream; }

typedef guest_ptr (*sysfunc_t)(void* /* guest cpu state */, void* /*jmptr*/);

class SyscallsModel : public Syscalls
{
public:
	SyscallsModel(
		const char* model_file,
		klee::KTestStream* kts, Guest* in_g);
	virtual ~SyscallsModel();
	virtual uint64_t apply(SyscallParams& sp);

	Guest* getGuest(void) const { return guest; }
	klee::KTestStream* getKTestStream(void) const { return kts; }
	void restoreCtx(void);
private:
	/* XXX: need this */
	// FileReconstructor	*file_recons;
	llvm::Module		*m;
	llvm::ExecutionEngine	*exe;
	sysfunc_t		sysf;
	klee::KTestStream	*kts;

	jmp_buf			restore_buf;
};

#endif
