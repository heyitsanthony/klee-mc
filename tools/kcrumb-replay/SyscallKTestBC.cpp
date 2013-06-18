#include "klee/Internal/ADT/KTestStream.h"
#include "klee/Internal/ADT/KTSFuzz.h"
#include "klee/Internal/ADT/Crumbs.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <string.h>
#include <sys/syscall.h>
#include "guestptimg.h"
#include "guestcpustate.h"

#include "SyscallKTestBC.h"

#define KBCREPLAY_NOTE	"[kbc-note]"

#define PFX	"[SyscallsKTestBC] "

using namespace klee;

KTestStream* SyscallsKTestBC::createKTS(const char* dirname, unsigned test_num)
{
	KTestStream	*kts;
	char		fname_ktest[256];
	const char	*corrupt;

	snprintf(
		fname_ktest,
		256,
		"%s/test%06d.ktest.gz", dirname, test_num);
	corrupt = getenv("KMC_CORRUPT_OBJ");
	if (corrupt == NULL)
		kts = KTestStream::create(fname_ktest);
	else {
		KTSFuzz		*ktsf;
		const char	*fuzz_percent;

		ktsf = KTSFuzz::create(fname_ktest);
		fuzz_percent = getenv("KMC_CORRUPT_PERCENT");
		ktsf->fuzzPart(
			atoi(corrupt),
			(fuzz_percent != NULL)
				? atof(fuzz_percent)/100.0
				: 0.5);
		kts = ktsf;
	}

	assert (kts != NULL && "Expects ktest");

	return kts;
}

SyscallsKTestBC::~SyscallsKTestBC()
{
	std::cerr << PFX "Total System Calls: " << syscall_c << '\n';
	std::cerr << PFX "Skipped in prologue: "  << prologue_c << '\n';
	std::cerr << PFX "Skipped in memops: "  << skipped_mem_c << '\n';
	std::cerr << PFX "Skipped total: " << skipped_c << '\n';
	std::cerr << PFX "Epilogue syscalls: " << epilogue_c << '\n';
	std::cerr << PFX "Log syscalls used: " << used_c << '\n';
}

#define BEGIN_FILE "/usr/lib64/valgrind/vgpreload_memcheck-amd64-linux.so"

/* eat all system calls until seeing an anchoring syscall from
 * valgrind (an open() on the memcheck so) */
bool SyscallsKTestBC::isReplayEnabled(SyscallParams& sp)
{
	char		str[1024];
	uint64_t	sys_nr = sp.getSyscall();

	if (seen_begin_file)
		return true;

	if (sys_nr != 2 /* open */)
		return false;

	/* display files being opened */
	memset(str, 0, sizeof(str));
	pt_r->getMem()->memcpy(str, guest_ptr(sp.getArg(0)), 1024);
	std::cerr << "open: " << str << '\n';

	/* NO DEFAULT SUPPRESSIONS (to mimic klee memcheck) */
#if 1
	if (strcmp(str, "/usr/lib64/valgrind/default.supp") == 0) {
		pt_r->slurpRegisters(pt_r->getPID());
		pt_r->getCPUState()->setSyscallResult(1234567);
		pt_r->pushRegisters();
	}
#endif


	if (strcmp(str, BEGIN_FILE) != 0)
		return false;


	seen_begin_file = true;
	return true;
}

struct bc_syscall* SyscallsKTestBC::peekSyscallCrumb(void)
{
	struct breadcrumb	*bc;
	struct bc_syscall	*bcs;

	do {
		bc = crumbs->peek();
		if (bc == NULL)
			return NULL;

		bcs = reinterpret_cast<struct bc_syscall*>(bc);
		assert (bc_is_type(bcs, BC_TYPE_SC));

		/* pass-through non-memory syscalls */
		if (	bcs->bcs_sysnr != SYS_mmap &&
			bcs->bcs_sysnr != SYS_munmap &&
			bcs->bcs_sysnr != SYS_mremap &&
			bcs->bcs_sysnr != SYS_brk)
			break;

		/* ignore syscall crumb + operations */
		std::cerr << PFX "Skipping sys=" << bcs->bcs_sysnr << '\n';
		crumbs->skip(bcs->bcs_op_c + 1);
		Crumbs::freeCrumb(bc);
		skipped_mem_c++;
	} while (1);

	return bcs;
}

bool SyscallsKTestBC::apply(void)
{
	syscall_c++;
	pt_r->slurpRegisters(pt_r->getPID());

	SyscallParams		sp(pt_r->getCPUState()->getSyscallParams());
	uint64_t		sys_nr = sp.getSyscall();
	struct bc_syscall	*bcs;
	bool			replayedSyscall = false;

	if (isReplayEnabled(sp) == false) {
		skipped_c++;
		prologue_c++;
		return false;
	}

	/* ran out of syscall crumbs; pass-through */
	if ((bcs = peekSyscallCrumb()) == NULL) {
		skipped_c++;
		epilogue_c++;
		return false;
	}
	
	/* match? */
	if (bcs->bcs_sysnr != sys_nr) {
#if 0
		std::cerr
			<< PFX "Skipping valgrind syscall sys_nr="
			<< sys_nr
			<< ". Expected sys call="
			<< bcs->bcs_sysnr
			<< '\n';
#endif
		skipped_c++;
	} else {
		replayedSyscall = true;
		used_c++;

//		std::cerr << "IN::::\n";
//		pt_r->getCPUState()->print(std::cerr);

		SyscallsKTest::apply(sp);

//		std::cerr << ":::::::::::::::OUT::::\n";
//		pt_r->getCPUState()->print(std::cerr);


		/* apply registers */
		pt_r->pushRegisters();
	}

	Crumbs::freeCrumb((struct breadcrumb*)bcs);
	return replayedSyscall;
}

SyscallsKTestBC* SyscallsKTestBC::create(
	PTImgRemote	*gpt,
	KTestStream	*k,
	const char	*test_path,
	unsigned	test_num)
{
	Crumbs		*c;
	unsigned	cn;
	char		fname_crumbs[256];
	
	if (!k) return NULL;

	cn = snprintf(
		fname_crumbs, 256, "%s/test%06d.crumbs.gz", 
		test_path, test_num);

	c = Crumbs::create(fname_crumbs);
	if (c == NULL) {
		fprintf(
			stderr,
			"[kmc] No breadcrumb file at %s. Faking it.\n",
			fname_crumbs);
		c = Crumbs::createEmpty();
	}
	assert (c != NULL && "Expects crumbs");

	if (!c) {
		delete k;
		return NULL;
	}

	return new SyscallsKTestBC(gpt, k, c);
}
