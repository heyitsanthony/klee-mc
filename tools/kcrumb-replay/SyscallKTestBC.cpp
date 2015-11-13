#include "klee/Internal/ADT/KTestStream.h"
#include "klee/Internal/ADT/KTSFuzz.h"
#include "klee/Internal/ADT/Crumbs.h"
#include <sys/mman.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/signal.h>
#include <string.h>
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
	std::string	str;
	uint64_t	sys_nr = sp.getSyscall();

	if (seen_begin_file)
		return true;


	if (sys_nr != SYS_open)
		return false;

	/* display files being opened */
	str = pt_r->getMem()->readString(guest_ptr(sp.getArg(0)));
	std::cerr << PFX "[Prologue] open: " << str << '\n';

	/* NO DEFAULT SUPPRESSIONS (to mimic klee memcheck) */
#if 1
	if (str == "/usr/lib64/valgrind/default.supp") {
		pt_r->slurpRegisters(pt_r->getPTArch()->getPID());
		pt_r->setSyscallResult(1234567);
		pt_r->getPTShadow()->pushRegisters();
	}
#endif


	if (str != BEGIN_FILE)
		return false;


	seen_begin_file = true;
	return false;
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

		switch (bcs->bcs_sysnr) {
		case SYS_mmap:
		case SYS_munmap:
		case SYS_mremap:
		case SYS_brk:
		case SYS_rt_sigaction:
			break;
		
		default:
			return bcs;
		}

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
	pt_r->slurpRegisters(pt_r->getPTArch()->getPID());

	SyscallParams		sp(pt_r->getSyscallParams());
	uint64_t		sys_nr = sp.getSyscall();
	struct bc_syscall	*bcs;
	bool			replayedSyscall = false;

	if (isReplayEnabled(sp) == false) {
		skipped_c++;
		prologue_c++;
		return false;
	}

	if (	sys_nr == SYS_kill
		|| sys_nr == SYS_tkill
		|| sys_nr == SYS_tgkill) {
		if (sp.getArg(0) == ~((uint32_t)0)) {
			std::cerr << PFX "target calling kill on pid=-1. No thanks!\n";
			exit(1);
		}
		if (sp.getArg(1) == SIGSTOP) {
			std::cerr << PFX "target calling SIGSTOP. No thanks!\n";
			exit(1);
		}
	}

	/* THIS IS NECESSARY TO AVOID KILLALL5 FROM KILLING ME */
#if 1
	if (sys_nr == SYS_mlockall) {
		if (sp.getArg(0) == (MCL_CURRENT|MCL_FUTURE)) {
			std::cerr << PFX " this is probably killall5!\n";
			exit(2);
		}
	}
#endif


#if 0
	static bool was_last_sigprocmask = false;
	if (sys_nr == SYS_rt_sigprocmask) {
		was_last_sigprocmask = true;
		skipped_c++;
		return false;
	}

	if (!was_last_sigprocmask) {
		skipped_c++;
		return false;
	}

	was_last_sigprocmask = false;

#endif

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
			<< PFX "Using valgrind syscall sys_nr="
			<< sys_nr
			<< ". Expected sys call="
			<< bcs->bcs_sysnr
			<< '\n';
#endif
		if (sys_nr == SYS_open)
			std::cerr << PFX "file name: "
				<< pt_r->getMem()->readString(
					guest_ptr(sp.getArg(0)))
				<< '\n';
		skipped_c++;
		goto done;
	}

	if (sys_nr == SYS_write || sys_nr == SYS_read) {
		int fd = sp.getArg(0);
		/* valgrind sema writes on fd=1028 confuse us:
		 * detect and ignore */
		if (fd == 1028 || fd == 1027) {
			skipped_c++;
			goto done;
		}
	}

	if (frameshift_c != 0) {
		frameshift_c--;
		std::cerr << PFX "frameshifted. Remaining: "
			<< frameshift_c << '\n';
		skipped_c++;
		goto done;
	}

	replayedSyscall = true;
	used_c++;

//	std::cerr << "IN::::\n";
//	pt_r->getCPUState()->print(std::cerr);

	SyscallsKTest::apply(sp);

//	std::cerr << ":::::::::::::::OUT::::\n";
//	pt_r->getCPUState()->print(std::cerr);

	/* apply registers */
	pt_r->getPTShadow()->pushRegisters();
done:
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
	assert(cn < 256);

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

SyscallsKTestBC::SyscallsKTestBC(
	PTImgRemote* g, KTestStream* _kts, Crumbs* _crumbs)
: SyscallsKTest(g, _kts, _crumbs)
, pt_r(g)
, seen_begin_file(false)
, prologue_c(0)
, epilogue_c(0)
, skipped_c(0)
, syscall_c(0) 
, skipped_mem_c(0)
, used_c(0)
, frameshift_c((getenv("KMC_FRAMESHIFT") != NULL)
	? atoi(getenv("KMC_FRAMESHIFT"))
	: 0)
{
}

