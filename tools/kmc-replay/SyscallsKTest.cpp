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

#include "klee/breadcrumb.h"
#include "klee/Internal/ADT/KTest.h"
#include "guestcpustate.h"
#include "guest.h"

#include "Crumbs.h"
#include "SyscallsKTest.h"

#define KREPLAY_NOTE	"[kmc-replay] "
#define KREPLAY_SC	"[kmc-sc] "

extern "C"
{
#include <valgrind/libvex_guest_amd64.h>
}

SyscallsKTest* SyscallsKTest::create(
	Guest* in_g,
	const char* fname_ktest,
	Crumbs* in_crumbs)
{
	SyscallsKTest	*skt;

	assert (in_g->getArch() == Arch::X86_64);

	skt = new SyscallsKTest(in_g, fname_ktest, in_crumbs);
	if (skt->ktest == NULL || skt->crumbs == NULL) {
		delete skt;
		skt = NULL;
	}

	return skt;
}

SyscallsKTest::SyscallsKTest(
	Guest* in_g,
	const char* fname_ktest,
	Crumbs* in_crumbs)
: Syscalls(in_g)
, ktest(NULL)
, sc_retired(0)
, crumbs(in_crumbs)
, bcs_crumb(NULL)
{
	ktest = kTest_fromFile(fname_ktest);
	if (ktest == NULL) return;
	next_ktest_obj = 0;
}

SyscallsKTest::~SyscallsKTest(void)
{
	if (ktest) kTest_free(ktest);
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
			sys_nr,
			sp.getArg(0),
			sp.getArg(1),
			sp.getArg(2));
		abort();
	}

	assert (bcs_crumb->bcs_hdr.bc_type == BC_TYPE_SC);

	if (bcs_crumb->bcs_sysnr != sys_nr) {
		fprintf(stderr,
			KREPLAY_NOTE
			"Mismatched: Got sysnr=%d. Expected sysnr=%d\n",
			sys_nr, bcs_crumb->bcs_sysnr);
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
	bool			ok;

	sop = reinterpret_cast<struct bc_sc_memop*>(crumbs->next());
	assert (sop != NULL && "Too few memops?");
	assert (sop->sop_hdr.bc_type == BC_TYPE_SCOP);

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

	delete sop;
}


//fprintf(stderr, "Faking syscall "#x" (%p,%p,%p)\n",
//	sp.getArg(0), sp.getArg(1), sp.getArg(2));


uint64_t SyscallsKTest::apply(SyscallParams& sp)
{
	uint64_t	ret;
	uint64_t	sys_nr;

	ret = 0;
	sys_nr = sp.getSyscall();

	fprintf(stderr, KREPLAY_NOTE"Applying: sys=%d\n", sys_nr);
	loadSyscallEntry(sp);

	/* extra thunks */
	switch(sys_nr) {
	case SYS_recvmsg:
		feedSyscallOp(sp);
		(((struct msghdr*)sp.getArg(1)))->msg_controllen = 0;
		break;
	case SYS_read:
		fprintf(stderr, KREPLAY_SC "READ ret=%p\n",getRet());
		break;

	case SYS_open:
		fprintf(stderr, KREPLAY_SC "OPEN \"%s\" ret=%p\n",
			sp.getArg(0), getRet());
		break;
	case SYS_write:
		fprintf(stderr,
			KREPLAY_SC "WRITING %d bytes to fd=%d :\"\n",
			sp.getArg(2), sp.getArg(0));
		write(STDERR_FILENO, (void*)sp.getArg(1), sp.getArg(2));
		fprintf(stderr, "\n" KREPLAY_SC "\".\n");

		break;
	case SYS_tgkill:
		if (sp.getArg(2) == SIGABRT)
			exited = true;
		break;
	case SYS_munmap:
		sc_munmap(sp);
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
			sys_nr);
		assert (0 == 1 && "TRICKY SYSCALL");
	}

	fprintf(stderr,
		KREPLAY_NOTE "Retired: sys=%d. ret=%p\n",
		sys_nr, getRet());
	
	sc_retired++;
	delete bcs_crumb;
	bcs_crumb = NULL;

	return ret;
}

void SyscallsKTest::sc_mmap(SyscallParams& sp)
{
	VexGuestAMD64State	*guest_cpu;
	void			*ret;
	bool			copied_in;

	if ((void*)bcs_crumb->bcs_ret != MAP_FAILED) {
		ret = mmap(
			(void*)bcs_crumb->bcs_ret,
			sp.getArg(1),
			PROT_READ | PROT_WRITE,
			MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS,
			-1,
			0);
		assert (ret != MAP_FAILED);
	} else {
		ret = (void*)bcs_crumb->bcs_ret;
	}

	guest_cpu = (VexGuestAMD64State*)guest->getCPUState()->getStateData();
	guest_cpu->guest_RAX = (uint64_t)ret;
	copied_in = copyInMemObj(guest_cpu->guest_RAX, sp.getArg(1));
	assert (copied_in && "BAD MMAP MEMOBJ");
}

void SyscallsKTest::sc_munmap(SyscallParams& sp)
{
	munmap((void*)sp.getArg(0), sp.getArg(1));
}

/* caller should know the size of the object based on
 * the syscall's context */
char* SyscallsKTest::feedMemObj(unsigned int sz)
{
	char			*obj_buf;
	struct KTestObject	*cur_obj;
	
	if (next_ktest_obj >= ktest->numObjects) {
		/* request overflow */
		fprintf(stderr, KREPLAY_NOTE"OF\n");
		return NULL;
	}

	cur_obj = &ktest->objects[next_ktest_obj++];
	if (cur_obj->numBytes != sz) {
		/* out of sync-- how to handle? */
		fprintf(stderr, KREPLAY_NOTE"OOSYNC: Expected: %d. Got: %d\n",
			sz,
			cur_obj->numBytes);
		return NULL;
	}

	obj_buf = new char[sz];
	memcpy(obj_buf, cur_obj->bytes, sz);
	fprintf(stderr, "NOM NOM %s (%d bytes)\n", 
		cur_obj->name,
		cur_obj->numBytes);

	return obj_buf;
}

bool SyscallsKTest::copyInMemObj(uint64_t guest_addr, unsigned int sz)
{
	char	*buf;

	/* first, grab mem obj */
	if ((buf = feedMemObj(sz)) == NULL)
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
	if ((partial_reg_buf = feedMemObj(reg_sz)) == NULL) {
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
