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
	const char* fname_ktest)
{
	SyscallsKTest	*skt;

	assert (in_g->getArch() == Arch::X86_64);

	skt = new SyscallsKTest(in_g, fname_ktest);
	if (skt->ktest == NULL) {
		delete skt;
		skt = NULL;
	}

	return skt;
}

SyscallsKTest::SyscallsKTest(
	Guest* in_g, 
	const char* fname_ktest)
: Syscalls(in_g)
 ,ktest(NULL)
 ,sc_retired(0)
{
	ktest = kTest_fromFile(fname_ktest);
	if (ktest == NULL) return;
	next_ktest_obj = 0;
}

SyscallsKTest::~SyscallsKTest(void)
{
	if (ktest) kTest_free(ktest);
}

uint64_t SyscallsKTest::apply(SyscallParams& sp)
{
	uint64_t	ret;
	uint64_t	sys_nr;

	ret = 0;
	sys_nr = sp.getSyscall();
	fprintf(stderr, "Applying: sys=%d\n", sys_nr);

	switch(sys_nr) {
	case SYS_open:
		copyInRegMemObj();
		break;
	case SYS_read:
		break;
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

	copied_in = copyInRegMemObj();
	assert (copied_in);

	guest_cpu = (VexGuestAMD64State*)guest->getCPUState()->getStateData();
	ret = mmap(
		(void*)guest_cpu->guest_RAX,
		sp.getArg(1),
		PROT_READ | PROT_WRITE,
		MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS,
		-1,
		0);
	assert (ret != MAP_FAILED);

	guest->getCPUState()->print(std::cerr);
	fprintf(stderr, "DESIRED SZ=%d\n", sp.getArg(1));
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
	
	fprintf(stderr, "feeed me %d\n", sz);
	if (next_ktest_obj >= ktest->numObjects) {
		/* request overflow */
		fprintf(stderr, "OF\n");
		return NULL;
	}

	cur_obj = &ktest->objects[next_ktest_obj++];
	if (cur_obj->numBytes != sz) {
		/* out of sync-- how to handle? */
		fprintf(stderr, "CUROBJ: %d\n", cur_obj->numBytes);
		return NULL;
	}

	obj_buf = new char[sz];
	memcpy(obj_buf, cur_obj->bytes, sz);

	return obj_buf;
}

/* XXX the vexguest stuff needs to be pulled into guestcpustate */
void SyscallsKTest::sc_stat(SyscallParams& sp)
{
	if (!copyInRegMemObj()) {
		fprintf(stderr, "failed to copy in reg mem obj\n");
		exited = true;
		fprintf(stderr, 
			"Do you have the right guest sshot for this ktest?");
		abort();
		return;
	}

	if (!copyInMemObj(sp.getArg(1), sizeof(struct stat))) {
		fprintf(stderr, "failed to copy in memobj\n");
		exited = true;
		fprintf(stderr, 
			"Do you have the right guest sshot for this ktest?");
		abort();
		return;
	}
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
