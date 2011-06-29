#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
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
	if (skt->ktest == NULL || skt->f_sclog == NULL) {
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
 ,ktest(NULL)
 ,f_sclog(NULL)
 ,sc_retired(0)
{
	/* TODO: gzip support */
	ktest = kTest_fromFile(fname_ktest);
	if (ktest == NULL) return;
	next_ktest_obj = 0;

	f_sclog = fopen(fname_sclog,  "r");
	if (f_sclog == NULL) return;
}

SyscallsKTest::~SyscallsKTest(void)
{
	if (ktest) kTest_free(ktest);
	if (f_sclog) fclose(f_sclog);
}

uint64_t SyscallsKTest::apply(SyscallParams& sp)
{
	uint64_t	ret;
	uint64_t	sys_nr;

	ret = 0;
	sys_nr = sp.getSyscall();


	if (!verifyCPURegs()) {
		fprintf(stderr, 
			"bad enter: sc=%d (retired %d)\n",
			sys_nr,
			sc_retired);
		exited = true;
		return -1;
	}

	switch(sys_nr) {
	case SYS_read:
		fprintf(stderr, "A %p %p\n", sp.getArg(0), sp.getArg(1));
		assert (0 == 1);
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

	if (!verifyCPURegs()) {
		fprintf(stderr,
			"bad leave: sc=%d (retired %d)\n",
			sys_nr);
		exited = true;
		return -1;
	}


	fprintf(stderr, "OK: sys=%d\n", sys_nr);
	sc_retired++;
	return ret;
}

bool SyscallsKTest::verifyCPURegs(void)
{
	void	*ktest_regs;
	int	cmp;

	ktest_regs = feedCPURegs();
	assert (ktest_regs);

	cmp = memcmp(
		ktest_regs, 
		guest->getCPUState()->getStateData(),
		guest->getCPUState()->getStateSize());
	if (cmp != 0) {
		std::cerr << "============Mismatch!============\n";
		std::cerr << "VEXEXEC: \n";
		guest->getCPUState()->print(std::cerr);
		std::cerr << "\nKLEE-MC: \n";
		guest->getCPUState()->print(std::cerr, ktest_regs);
		std::cerr << "\n";
		return false;
	}

	delete ktest_regs;
	return true;
}

/* allocate new register context based on what we read from sclog */
char* SyscallsKTest::feedCPURegs(void)
{
	char*		reg_buf;
	unsigned int	reg_sz;
	ssize_t		br;
	
	reg_sz = guest->getCPUState()->getStateSize();
	reg_buf = new char[reg_sz];

	br = fread(reg_buf, reg_sz, 1, f_sclog);
	if (br <= 0) {
		delete reg_buf;
		return NULL;
	}

	return reg_buf;
}

/* caller should know the size of the object based on 
 * the syscall's context */
char* SyscallsKTest::feedMemObj(unsigned int sz)
{
	char			*obj_buf;
	struct KTestObject	*cur_obj;
	
	if (next_ktest_obj >= ktest->numObjects) {
		/* request overflow */
		return NULL;
	}

	cur_obj = &ktest->objects[next_ktest_obj++];
	if (cur_obj->numBytes != sz) {
		/* out of sync-- how to handle? */
		return NULL;
	}

	obj_buf = new char[sz];
	memcpy(obj_buf, cur_obj->bytes, sz);

	return obj_buf;
}

/* XXX the vexguest stuff needs to be pulled into guestcpustate */
void SyscallsKTest::sc_stat(SyscallParams& sp)
{
	if (!copyInMemObj(sp.getArg(1), sizeof(struct stat))) {
		fprintf(stderr, "failed to copy in memobj\n");
		exited = true;
		return;
	}

	if (!copyInRegMemObj()) {
		fprintf(stderr, "failed to copy in reg mem obj\n");
		exited = true;
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
