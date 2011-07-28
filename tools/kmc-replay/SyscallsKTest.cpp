#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <unistd.h>

#include "klee/Internal/ADT/KTest.h"
#include "guestcpustate.h"
#include "guest.h"

#include "SyscallsKTest.h"

extern "C"
{
#include <valgrind/libvex_guest_amd64.h>
}

SyscallsKTest* SyscallsKTest::create(
	Guest* in_g,
	const char* fname_ktest,
	const char* fname_sclog)
{
	SyscallsKTest	*skt;

	assert (in_g->getArch() == Arch::X86_64);

	skt = new SyscallsKTest(in_g, fname_ktest, fname_sclog);
	if (skt->ktest == NULL || skt->sclog == NULL) {
		delete skt;
		skt = NULL;
	}

	return skt;
}

SyscallsKTest::SyscallsKTest(
	Guest* in_g,
	const char* fname_ktest,
	const char* fname_sclog)
: Syscalls(in_g)
, ktest(NULL)
, sc_retired(0)
, sclog(NULL)
{
	ktest = kTest_fromFile(fname_ktest);
	if (ktest == NULL) return;

	sclog = fopen(fname_sclog, "rb");
	if (sclog == NULL) return;

	next_ktest_obj = 0;
}

SyscallsKTest::~SyscallsKTest(void)
{
	if (ktest) kTest_free(ktest);
	if (sclog) fclose(sclog);
}

void SyscallsKTest::loadSyscallEntry(SyscallParams& sp)
{
	uint64_t sys_nr = sp.getSyscall();

	if (!copyInSCEntry()) {
		fprintf(stderr,
			"Could not read sclog entry #%d. %p %p %p\n",
			sc_retired,
			sp.getArg(0),
			sp.getArg(1),
			sp.getArg(2));
		abort();
	}

	if (sce.sce_sysnr != sys_nr) {
		fprintf(stderr,
			"Mismatched: Got sysnr=%d. Expected sysnr=%d\n",
			sys_nr, sce.sce_sysnr);
	}

	assert (sce.sce_sysnr == sys_nr && "sysnr mismatch with log");
	if (sce.sce_flags & SC_FL_NEWREGS) {
		bool	reg_ok;

		reg_ok = copyInRegMemObj();
		if (!reg_ok) {
			fprintf(stderr, "Could not copy in regmemobj\n");
			exited = true;
			fprintf(stderr,
				"Wrong guest sshot for this ktest?\n");
			abort();
			return;
		}
	}

}

//fprintf(stderr, "Faking syscall "#x" (%p,%p,%p)\n",	\
//	sp.getArg(0), sp.getArg(1), sp.getArg(2));	\


#define FAKE_SC(x) 		\
	case SYS_##x:		\
	setRet(0);		\
	break;


uint64_t SyscallsKTest::apply(SyscallParams& sp)
{
	uint64_t	ret;
	uint64_t	sys_nr;

	ret = 0;
	sys_nr = sp.getSyscall();

	fprintf(stderr, "Applying: sys=%d\n", sys_nr);
	loadSyscallEntry(sp);

	switch(sys_nr) {
	case SYS_close: break;
	case SYS_read:
		if (getRet() != -1) {
			bool	ok;
			ok = copyInMemObj(sp.getArg(1), sp.getArg(2));
			assert (ok && "OOPS BAD READ");
		}
		break;
	case SYS_brk:
		setRet(-1);
		break;
	case SYS_open:
		fprintf(stderr, "OPEN: \"%s\" ret=%p\n",
			sp.getArg(0), getRet());
		break;
	case SYS_write:
		fprintf(stderr, "WRITING %d bytes to fd=%d \"%s\"\n",
			sp.getArg(2), sp.getArg(0), sp.getArg(1));
		break;
	FAKE_SC(rt_sigaction)
	case SYS_munmap:
		sc_munmap(sp);
		break;
	case SYS_mmap:
		sc_mmap(sp);
		break;
	case SYS_fstat:
	case SYS_stat:
		sc_stat(sp);
		break;
	case SYS_exit:
		exited = true;
		break;
	default:
		fprintf(stderr, "Tried to do syscall %d\n", sys_nr);
		assert (0 == 1 && "TRICKY SYSCALL");
	}

retire:
	fprintf(stderr, "Retired: sys=%d\n", sys_nr);
	sc_retired++;
	return ret;
}

void SyscallsKTest::sc_mmap(SyscallParams& sp)
{
	char			*obj_buf;
	VexGuestAMD64State	*guest_cpu;
	void			*ret;
	bool			copied_in;

	if ((void*)sce.sce_ret != MAP_FAILED) {
		ret = mmap(
			(void*)sce.sce_ret,
			sp.getArg(1),
			PROT_READ | PROT_WRITE,
			MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS,
			-1,
			0);
		assert (ret != MAP_FAILED);
	} else {
		ret = (void*)sce.sce_ret;
	}

	guest_cpu = (VexGuestAMD64State*)guest->getCPUState()->getStateData();
	guest_cpu->guest_RAX = (uint64_t)ret;
	copied_in = copyInMemObj(guest_cpu->guest_RAX, sp.getArg(1));
	assert (copied_in && "BAD MMAP MEMOBJ");
}

void SyscallsKTest::sc_munmap(SyscallParams& sp)
{
	assert (0 ==1 && "STUB");
}

/* caller should know the size of the object based on
 * the syscall's context */
char* SyscallsKTest::feedMemObj(unsigned int sz)
{
	char			*obj_buf;
	struct KTestObject	*cur_obj;
	
	if (next_ktest_obj >= ktest->numObjects) {
		/* request overflow */
		fprintf(stderr, "OF\n");
		return NULL;
	}

	cur_obj = &ktest->objects[next_ktest_obj++];
	if (cur_obj->numBytes != sz) {
		/* out of sync-- how to handle? */
		fprintf(stderr, "OOSYNC: Expected: %d. Got: %d\n",
			sz,
			cur_obj->numBytes);
		return NULL;
	}

	obj_buf = new char[sz];
	memcpy(obj_buf, cur_obj->bytes, sz);

	return obj_buf;
}

/* XXX the vexguest stuff needs to be pulled into guestcpustate */
void SyscallsKTest::sc_stat(SyscallParams& sp)
{
	if (getRet() != 0) return;

	if (!copyInMemObj(sp.getArg(1), sizeof(struct stat))) {
		fprintf(stderr, "failed to copy in memobj\n");
		exited = true;
		fprintf(stderr,
			"Do you have the right guest sshot for this ktest?");
		abort();
		return;
	}
}

bool SyscallsKTest::copyInSCEntry(void)
{
	size_t	br;
	br = fread(&sce, sizeof(sce), 1, sclog);
	return (br == 1);
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
