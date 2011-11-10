#include <poll.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>

#include "klee/klee.h"
#include "klee/breadcrumb.h"
#include "klee/Internal/ADT/KTestStream.h"
#include "klee/Internal/ADT/Crumbs.h"
#include "guestcpustate.h"
#include "guest.h"

#include "SyscallsKTest.h"

#define KREPLAY_NOTE	"[kmc-replay] "
#define KREPLAY_SC	"[kmc-sc] "

using namespace klee;

extern "C"
{
#include <valgrind/libvex_guest_amd64.h>
}

SyscallsKTest* SyscallsKTest::create(
	Guest* in_g,
	KTestStream* kts,
	Crumbs* in_crumbs)
{
	SyscallsKTest	*skt;

	assert (in_g->getArch() == Arch::X86_64);

	skt = new SyscallsKTest(in_g, kts, in_crumbs);
	if (skt->kts == NULL || skt->crumbs == NULL) {
		delete skt;
		skt = NULL;
	}

	return skt;
}

SyscallsKTest::SyscallsKTest(
	Guest* in_g,
	KTestStream* in_kts,
	Crumbs* in_crumbs)
: Syscalls(in_g)
, kts(in_kts)
, sc_retired(0)
, crumbs(in_crumbs)
, bcs_crumb(NULL)
{
}

SyscallsKTest::~SyscallsKTest(void)
{
	if (kts) delete kts;
	if (bcs_crumb) delete bcs_crumb;
}

void SyscallsKTest::badCopyBail(void)
{
	fprintf(stderr,
		KREPLAY_NOTE
		"Could not copy in memobj\n");
	exited = true;
	fprintf(stderr,
		KREPLAY_NOTE
		"Wrong guest sshot for this ktest?\n");
	abort();
}

void SyscallsKTest::loadSyscallEntry(SyscallParams& sp)
{
	uint64_t sys_nr = sp.getSyscall();

	assert (bcs_crumb == NULL && "Last crumb should be freed before load");
	bcs_crumb = reinterpret_cast<struct bc_syscall*>(crumbs->next());
	if (!bcs_crumb) {
		fprintf(stderr,
			KREPLAY_NOTE
			"Could not read sclog entry #%d. Out of entries.\n"
			KREPLAY_NOTE
			"sys_nr=%d. arg[0]=%p arg[1]=%p arg[2]=%p\n",
			sc_retired,
			(int)sys_nr,
			sp.getArgPtr(0),
			sp.getArgPtr(1),
			sp.getArgPtr(2));
		abort();
	}

	assert (bc_is_type(bcs_crumb, BC_TYPE_SC));

	if (bcs_crumb->bcs_sysnr != sys_nr) {
		fprintf(stderr,
			KREPLAY_NOTE
			"Mismatched: Got sysnr=%d. Expected sysnr=%d\n",
			(int)sys_nr, (int)bcs_crumb->bcs_sysnr);
	}

	assert (bcs_crumb->bcs_sysnr == sys_nr && "sysnr mismatch with log");

	/* setup new register context */
	if (bc_sc_is_newregs(bcs_crumb)) {
		if (!copyInRegMemObj())
			badCopyBail();
	} else {
		setRet(bcs_crumb->bcs_ret);
	}

	/* read in any objects written out */
	/* note that thunked calls need to manage mem objects specially */
	if (!bc_sc_is_thunk(bcs_crumb)) {
		for (unsigned int i = 0; i < bcs_crumb->bcs_op_c; i++) {
			feedSyscallOp(sp);
		}
	}
}

void SyscallsKTest::feedSyscallOp(SyscallParams& sp)
{
	struct bc_sc_memop	*sop;
	uint64_t		dst_ptr;
	unsigned int		flags;

	sop = reinterpret_cast<struct bc_sc_memop*>(crumbs->next());
	assert (sop != NULL && "Too few memops?");

	/* dump some convenient debugging info if something fucked up */
	if (!bc_is_type(sop, BC_TYPE_SCOP)) {
		BCrumb	*bc = Crumbs::toBC((struct breadcrumb*)sop);
		std::cerr << "UNEXPECTED BREADCRUMB. EXPECTED SCOP.\n GOT:\n";
		bc->print(std::cerr);
	}
	assert (bc_is_type(sop, BC_TYPE_SCOP));

	flags = sop->sop_hdr.bc_type_flags;
	if (flags & BC_FL_SCOP_USERPTR) {
		dst_ptr = (uint64_t)sop->sop_baseptr.ptr;
	} else if (flags & BC_FL_SCOP_ARGPTR) {
		dst_ptr = sp.getArg(sop->sop_baseptr.ptr_sysarg);
	} else {
		assert (0 == 1 && "Bad syscall op flags");
	}

	if (!copyInMemObj((uint64_t)dst_ptr + sop->sop_off, sop->sop_sz))
		badCopyBail();

	Crumbs::freeCrumb(&sop->sop_hdr);
}

uint64_t SyscallsKTest::apply(SyscallParams& sp)
{
	uint64_t	ret;
	uint64_t	sys_nr;
	ssize_t		bw;

	ret = 0;
	sys_nr = sp.getSyscall();

	if (sys_nr != SYS_klee)
		fprintf(stderr, KREPLAY_NOTE"Applying: sys=%d\n", (int)sys_nr);
	else
		fprintf(stderr, KREPLAY_NOTE"Applying: sys=SYS_klee\n");

	loadSyscallEntry(sp);

	/* GROSS UGLY HACK. OH WELL. */
	if (bc_sc_is_thunk(bcs_crumb) && sys_nr != SYS_recvmsg)
		crumbs->skip(bcs_crumb->bcs_op_c);

	/* extra thunks */
	switch(sys_nr) {
	case SYS_recvmsg:
		printf("HELLO RECVMSG!!!\n");
		feedSyscallOp(sp);
		(((struct msghdr*)sp.getArg(1)))->msg_controllen = 0;
		printf("BYEBYE MY MANG\n");
		break;
	case SYS_read:
		fprintf(stderr, KREPLAY_SC "READ ret=%p\n", (void*)getRet());
		break;

	case SYS_open:
		fprintf(stderr, KREPLAY_SC "OPEN \"%s\" ret=%p\n",
			(char*)sp.getArgPtr(0), (void*)getRet());
		break;
	case SYS_write:
		fprintf(stderr,
			KREPLAY_SC "WRITING %d bytes to fd=%d :\"\n",
			(int)sp.getArg(2), (int)sp.getArg(0));
		bw = write(STDERR_FILENO, sp.getArgPtr(1), sp.getArg(2));
		fprintf(stderr, "\n" KREPLAY_SC "\".\n");

		break;
	case SYS_tgkill:
		if (sp.getArg(2) == SIGABRT)
			exited = true;
		break;
	case SYS_poll: {
		struct pollfd*	fds = (struct pollfd*)(sp.getArg(0));
		unsigned int	nfds = sp.getArg(1);
		for (unsigned int i = 0; i < nfds; i++)
			fds[i].revents = fds[i].events;
		break;
	}

	case SYS_getcwd: {
		if (!bc_sc_is_thunk(bcs_crumb)) break;

		uint64_t addr = bcs_crumb->bcs_ret;
		assert (addr != 0 && "Tricky getcwd with a NULL return?");

		uint64_t len = sp.getArg(1);
		if (len > 10) len = 10;

		feedSyscallOp(sp);
		fprintf(stderr, "ADDR=%p LEN=%d ARG0=%p. ARG1=%p\n",
			(void*)addr, (int)len, sp.getArgPtr(0), sp.getArgPtr(1));
		if (addr != sp.getArg(0)) {
			fprintf(stderr, "LOOOOOOOOOOOOOOOOL\n");
		}
		((char*)addr)[len-1] = '\0';

		setRet(addr);
		break;
	}
	case SYS_mprotect:
		guest->getMem()->mprotect(
			guest_ptr(sp.getArg(0)),
			sp.getArg(1),
			sp.getArg(2));
		break;
	case SYS_munmap:
		fprintf(stderr, KREPLAY_SC"MUNMAPPING: %p-%p\n",
			(void*)sp.getArg(0),
			(void*)(sp.getArg(0) + sp.getArg(1)));
		guest->getMem()->munmap(guest_ptr(sp.getArg(0)), sp.getArg(1));
		break;
	case SYS_mmap:
		sc_mmap(sp);
		break;
	case SYS_exit_group:
	case SYS_exit:
		exited = true;
		break;
	default:
		if (!bc_sc_is_thunk(bcs_crumb)) break;
		fprintf(stderr,
			KREPLAY_NOTE "No thunk for syscall %d\n",
			(int)sys_nr);
		assert (0 == 1 && "TRICKY SYSCALL");
	}

	fprintf(stderr,
		KREPLAY_NOTE "Retired: sys=%d. ret=%p\n",
		(int)sys_nr, (void*)getRet());

	sc_retired++;
	Crumbs::freeCrumb(&bcs_crumb->bcs_hdr);
	bcs_crumb = NULL;

	return ret;
}

void SyscallsKTest::sc_mmap(SyscallParams& sp)
{
	void		*bcs_ret;
	guest_ptr	g_ret;
	int		rc;

	bcs_ret = (void*)bcs_crumb->bcs_ret;
	if (bcs_ret == MAP_FAILED) {
		setRet((uint64_t)bcs_ret);
		return;
	}

	fprintf(stderr, KREPLAY_SC" MMAP fd=%d flags=%x TO %p\n",
		(int)sp.getArg(4),
		(int)sp.getArg(3),
		bcs_ret);
	rc = guest->getMem()->mmap(
		g_ret,
		guest_ptr((uint64_t)bcs_ret),
		sp.getArg(1),
		PROT_READ | PROT_WRITE,
		((bcs_ret) ? MAP_FIXED : 0)
			| MAP_PRIVATE
			| MAP_ANONYMOUS,
		-1,
		0);

	if (rc != 0 || (void*)g_ret.o == MAP_FAILED) {
		fprintf(stderr,
			"MAP FAILED ON FIXED ADDR %p bytes=%p. rc=%d\n",
			bcs_ret, sp.getArgPtr(1), rc);
	}
	assert (rc == 0);

	setRet((uint64_t)g_ret.o);

	if (((int)sp.getArg(4)) != -1) {
		/* only symbolic if fd is defined (e.g. fd != -1) */
		bool	copied_in;
		copied_in = copyInMemObj(g_ret.o, sp.getArg(1));
		assert (copied_in && "BAD MMAP MEMOBJ");
	}
}

bool SyscallsKTest::copyInMemObj(uint64_t guest_addr, unsigned int sz)
{
	char	*buf;

	/* first, grab mem obj */
	if ((buf = kts->feedObjData(sz)) == NULL)
		return false;

	guest->getMem()->memcpy(guest_ptr(guest_addr), buf, sz);

	delete [] buf;
	return true;
}

void SyscallsKTest::setRet(uint64_t r)
{
	VexGuestAMD64State	*guest_cpu;
	guest_cpu = (VexGuestAMD64State*)guest->getCPUState()->getStateData();
	guest_cpu->guest_RAX = r;
}

uint64_t SyscallsKTest::getRet(void) const
{
	VexGuestAMD64State	*guest_cpu;
	guest_cpu = (VexGuestAMD64State*)guest->getCPUState()->getStateData();
	return guest_cpu->guest_RAX;
}

bool SyscallsKTest::copyInRegMemObj(void)
{
	char			*partial_reg_buf;
	VexGuestAMD64State	*partial_cpu, *guest_cpu;
	unsigned int		reg_sz;

	reg_sz = guest->getCPUState()->getStateSize();
	if ((partial_reg_buf = kts->feedObjData(reg_sz)) == NULL) {
		return false;
	}
	partial_cpu = (VexGuestAMD64State*)partial_reg_buf;

	/* load RAX, RCX, R11 */
	guest_cpu = (VexGuestAMD64State*)guest->getCPUState()->getStateData();
	guest_cpu->guest_RAX = partial_cpu->guest_RAX;
	guest_cpu->guest_RCX = partial_cpu->guest_RCX;
	guest_cpu->guest_R11 = partial_cpu->guest_R11;

	delete [] partial_reg_buf;
	return true;
}
