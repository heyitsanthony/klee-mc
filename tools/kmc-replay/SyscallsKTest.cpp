#include <poll.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <asm/ldt.h>
#include <stdlib.h>
#include <unistd.h>

#include "klee/klee.h"
#include "klee/breadcrumb.h"
#include "klee/Internal/ADT/KTestStream.h"
#include "klee/Internal/ADT/Crumbs.h"
#include "FileReconstructor.h"
#include "guestcpustate.h"
#include "guest.h"
#include "cpu/i386windowsabi.h"
#include "cpu/i386_macros.h"

#include "SyscallsKTest.h"

#define TMP_ARG_CSTR(n)	\
	guest->getMem()->readString(guest_ptr(sp.getArg((n)))).c_str()

using namespace klee;

extern "C"
{
#include <valgrind/libvex_guest_amd64.h>
#include <valgrind/libvex_guest_arm.h>
#include <valgrind/libvex_guest_x86.h>
}

SyscallsKTest* SyscallsKTest::create(
	Guest* in_g,
	KTestStream* kts,
	Crumbs* in_crumbs)
{
	SyscallsKTest	*skt;

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
, file_recons((getenv("KMC_RECONS_FILES") != NULL)
	? new FileReconstructor()
	: NULL)
, kts(in_kts)
, crumbs(in_crumbs)
, sc_retired(0)
, bcs_crumb(NULL)
, last_brk(0)
, concrete_vfs((getenv("KMC_CONCRETE_VFS") != NULL)
	? new ConcreteVFS()
	: NULL)
{
	is_w32 = dynamic_cast<const I386WindowsABI*>(in_g->getABI());
}

SyscallsKTest::~SyscallsKTest(void)
{
	if (kts) delete kts;
	if (bcs_crumb) delete bcs_crumb;
	if (file_recons) delete file_recons;
	if (concrete_vfs) delete concrete_vfs;
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

int SyscallsKTest::loadSyscallEntry(SyscallParams& sp)
{
	uint64_t sys_nr = sp.getSyscall();

	assert (bcs_crumb == NULL && "Last crumb should be freed before load");
	bcs_crumb = reinterpret_cast<struct bc_syscall*>(crumbs->next());
	if (!bcs_crumb) {
		fprintf(stderr,
			KREPLAY_NOTE
			"Could not read sclog entry #%d. Out of entries.\n"
			KREPLAY_NOTE
			"sys_nr=%d. xsys=???. arg[0]=%p arg[1]=%p arg[2]=%p\n",
			sc_retired,
			(int)sys_nr,
			sp.getArgPtr(0),
			sp.getArgPtr(1),
			sp.getArgPtr(2));
		abort();
	}

	if (!bc_is_type(bcs_crumb, BC_TYPE_SC)) {
		BCrumb	*bc = Crumbs::toBC((struct breadcrumb*)bcs_crumb);
		bc->print(std::cerr);
	}
	assert (bc_is_type(bcs_crumb, BC_TYPE_SC));

	if (bcs_crumb->bcs_sysnr != sys_nr) {
		fprintf(stderr,
			KREPLAY_NOTE
			"Mismatched: Got sysnr=%d. Logged sysnr=%d\n",
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
	if (!bc_sc_is_thunk(bcs_crumb))
		feedSyscallOpBCS(sp);

	return bcs_crumb->bcs_xlate_sysnr;
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


void SyscallsKTest::feedSyscallOpBCS(SyscallParams& sp)
{
	for (unsigned int i = 0; i < bcs_crumb->bcs_op_c; i++)
		feedSyscallOp(sp);
}

uint64_t SyscallsKTest::apply(SyscallParams& sp)
{
	uint64_t	ret;
	uint64_t	sys_nr;
	int		xlate_sysnr;

	ret = 0;
	sys_nr = sp.getSyscall();

	if (sys_nr != SYS_klee)
		fprintf(stderr, KREPLAY_NOTE"Applying: sys=%d\n", (int)sys_nr);
	else
		fprintf(stderr, KREPLAY_NOTE"Applying: sys=SYS_klee\n");

	xlate_sysnr = loadSyscallEntry(sp);

	/* extra thunks */
	if (!is_w32) {
		/* GROSS UGLY HACK. OH WELL. */
		if (	bc_sc_is_thunk(bcs_crumb)
			&& xlate_sysnr != SYS_recvmsg
			&& xlate_sysnr != SYS_recvfrom
			&& xlate_sysnr != SYS_getcwd
			&& xlate_sysnr != SYS_getsockname)
		{
			crumbs->skip(bcs_crumb->bcs_op_c);
		}

		if (	!concrete_vfs ||
			!concrete_vfs->apply(guest, sp, xlate_sysnr))
		{
			doLinuxThunks(sp, xlate_sysnr);
		}
	} else {
		GuestCPUState	*cpu(guest->getCPUState());

		if (bc_sc_is_thunk(bcs_crumb))
			crumbs->skip(bcs_crumb->bcs_op_c);

		/* HACK AHCKACHACHKAC */
		if (cpu->getExitType() == GE_SYSCALL) {
			cpu->setPC(guest_ptr(
				cpu->getReg("IP_AT_SYSCALL", 32) + 2));
			cpu->setReg("EDX", 32, cpu->getReg("EDX", 32) + 8);
		}
	}


	fprintf(stderr,
		KREPLAY_NOTE "Retired: sys=%d. xsys=%d. ret=%p.\n",
		(int)sys_nr,
		(int)xlate_sysnr,
		(void*)getRet());

	sc_retired++;
	Crumbs::freeCrumb(&bcs_crumb->bcs_hdr);
	bcs_crumb = NULL;

	return ret;
}

void SyscallsKTest::doLinuxThunks(SyscallParams& sp, int xlate_sysnr)
{
	ssize_t		bw;

	switch(xlate_sysnr) {
	case SYS_set_thread_area: {
		struct user_desc	ud;
		VexGuestX86State*	regs;
		VexGuestX86SegDescr	vs, *gvs;
		
		regs = (VexGuestX86State*)guest->getCPUState()->getStateData();
		gvs = static_cast<VexGuestX86SegDescr*>(
			((void*)((regs)->guest_GDT)));

		guest->getMem()->memcpy(
			&ud, guest_ptr(sp.getArg(0)), sizeof(ud));

		if ((int)ud.entry_number == -1) {
			int	i;
			/* find free entry */
			for (i = 1; i < 1024; i++) {
				guest->getMem()->memcpy(
					&vs,
					guest_ptr((long)&gvs[i]),
					sizeof(vs));

				if (vs.LdtEnt.Bits.Pres == 0)
					break;
			}

			ud.entry_number = i;
			guest->getMem()->memcpy(
				guest_ptr(sp.getArg(0)), &ud, sizeof(ud));

			std::cerr << "[kmc] set entry_number=" << i << '\n';
		}

		ud2vexseg(ud, &vs.LdtEnt);

		gvs = &gvs[ud.entry_number];

		guest->getMem()->memcpy(
			guest_ptr((uintptr_t)gvs), &vs, sizeof(vs));

		guest->getMem()->memcpy(&vs,
			guest_ptr((uintptr_t)gvs), sizeof(vs));
		break;
	}
	case SYS_uname: {
		guest_ptr	p(sp.getArg(0));
#define UNAME_COPY(x,y)	guest->getMem()->memcpy(	\
	p+offsetof(struct utsname,x),y,strlen(y)+1)
		
		UNAME_COPY(sysname, "Linux");
		UNAME_COPY(nodename, "kleemc");
		UNAME_COPY(release, "3.9.6");
		UNAME_COPY(version, "x");
		UNAME_COPY(machine, "x86_64");
		break;
	}

	case SYS_getsockname:
		if (getRet() == 0) {
			feedSyscallOp(sp);
			*((socklen_t*)sp.getArgPtr(2)) =sizeof(struct sockaddr_in);
		} else
			*((socklen_t*)sp.getArgPtr(2)) = 0;
		break;

	case SYS_arch_prctl: {
		VexGuestAMD64State	*guest_cpu;
		guest_cpu = (VexGuestAMD64State*)guest->getCPUState()->getStateData();
		assert (guest->getArch()== Arch::X86_64);
		guest_cpu->guest_FS_ZERO = sp.getArg(1);
		break;
	}
	case SYS_recvfrom:
		feedSyscallOp(sp);
		if (sp.getArgPtr(4) != NULL)
			feedSyscallOp(sp);
		if (sp.getArgPtr(5) != NULL) {
			guest->getMem()->writeNative(
				guest_ptr(sp.getArg(5)),
				sizeof(struct sockaddr_in));
		}
		setRet(sp.getArg(2));
		break;
	case SYS_recvmsg:
		feedSyscallOpBCS(sp);
		break;

	case SYS_brk: {
		uint64_t	new_brk;
		int		mmap_ret;
		guest_ptr	ret;

		new_brk = getRet();
		if (last_brk == 0) {
			/* first brk(), don't do much */
			last_brk = new_brk;
			break;
		}

		/* make things page-aligned so mmap won't complain */
		last_brk = last_brk & ~0xfff;
		new_brk = 4096*((new_brk + 4095) / 4096);

		if (last_brk >= new_brk) {
			std::cerr << "[SyscallsKTest] Warning: Old Brk "
				<< (void*)last_brk << " >= "
				<< (void*)new_brk << '\n';
			break;
		}

		/* extend program break */
		mmap_ret = guest->getMem()->mmap(
			ret,
			guest_ptr(last_brk),
			new_brk - last_brk,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS,
			-1, 0);

		if ((void*)ret.o != (void*)last_brk)
			std::cerr << "Brks not equal: ret.o=" <<
				(void*)ret.o << " vs. last_brk=" <<
				(void*)last_brk << ". new_brk =" <<
				(void*)new_brk << '\n';

		assert ((void*)ret.o == (void*)last_brk);
		last_brk = new_brk;
		break;
	}


	case SYS_lseek:
		if (file_recons != NULL)
			file_recons->seek(
				sp.getArg(0), sp.getArg(1), sp.getArg(2));
		fprintf(stderr, KREPLAY_SC "SEEK TO OFF=%p\n", (void*)sp.getArg(1));
		break;

	case SYS_read: {
		fprintf(stderr, KREPLAY_SC "READ fd=%d. ret=%p.\n",
			(int)sp.getArg(0),
			(void*)getRet());
		if (file_recons != NULL)
			file_recons->read(
				sp.getArg(0), sp.getArgPtr(1), getRet());
		break;
	}

	case SYS_close:
		fprintf(stderr, KREPLAY_SC "CLOSE fd=%p\n", sp.getArgPtr(0));
		if (file_recons) {
			file_recons->close(sp.getArg(0));
		}
		break;

	case SYS_lstat:
	case SYS_stat:
		fprintf(stderr, KREPLAY_SC "stat \"%s\" ret=%p\n",
			TMP_ARG_CSTR(0),
			(void*)getRet());
		break;

	case SYS_readlinkat:
	case SYS_readlink:
		fprintf(stderr, KREPLAY_SC "READLINK \"%s\" ret=%p\n",
			TMP_ARG_CSTR((xlate_sysnr == SYS_readlink) ? 0 : 1),
			(void*)getRet());
		break;

	case SYS_open:
		fprintf(stderr, KREPLAY_SC "OPEN \"%s\" ret=%p\n",
			TMP_ARG_CSTR(0), (void*)getRet());
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

	case SYS_getcwd: {
		if (!bc_sc_is_thunk(bcs_crumb)) break;

		uint64_t addr = bcs_crumb->bcs_ret;
		assert (addr != 0 && "Tricky getcwd with a NULL return?");

		uint64_t len = sp.getArg(1);
		if (len > 10) len = 10;

		feedSyscallOp(sp);

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
/* XXX: think of a better place to put this-- from runtime/syscall/syscalls.h */
#define ARCH_mmap2	0x1000
	case ARCH_mmap2:
		sp.setArg(5, sp.getArg(5)*4096);
	case SYS_mmap:
		sc_mmap(sp);
		break;
	case SYS_execve: {
		std::string	s;
		s = guest->getMem()->readString(guest_ptr(sp.getArg(0)));
		/* XXX: be more descriptive? */
		fprintf(stderr,
			KREPLAY_NOTE "exiting on execve(%s);\n",
			s.c_str());
	}
	case SYS_exit_group:
	case SYS_exit:
		exited = true;
		break;
	default:
		if (!bc_sc_is_thunk(bcs_crumb)) break;
		fprintf(stderr,
			KREPLAY_NOTE "No thunk for syscall sys=%d. xsys=%d.\n",
			(int)sp.getSyscall(),
			(int)xlate_sysnr);
		assert (0 == 1 && "TRICKY SYSCALL");
	}
}

void SyscallsKTest::sc_mmap(SyscallParams& sp)
{
	void		*bcs_ret;
	guest_ptr	g_ret;
	int		rc;

	bcs_ret = (void*)bcs_crumb->bcs_ret;
	if (((int32_t)(intptr_t)bcs_ret) == ((int32_t)(intptr_t)MAP_FAILED)) {
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

	if (((int32_t)sp.getArg(4)) != (int32_t)-1) {
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
	void	*state_data;
	Arch::Arch	arch;

	state_data = guest->getCPUState()->getStateData();

	arch = guest->getArch();
	if (arch == Arch::X86_64) {
		VexGuestAMD64State	*guest_cpu;
		guest_cpu = (VexGuestAMD64State*)state_data;
		guest_cpu->guest_RAX = r;
		return;
	}
	if (arch == Arch::ARM) {
		VexGuestARMState	*guest_cpu;
		guest_cpu = (VexGuestARMState*)state_data;
		guest_cpu->guest_R0 = (uint32_t)r;
		return;
	}

	if (arch == Arch::I386) {
		VexGuestX86State	*guest_cpu;
		guest_cpu = (VexGuestX86State*)state_data;
		guest_cpu->guest_EAX = (uint32_t)r;
		return;
	}

	assert (0 == 1 && "UNK ARCH");
}

uint64_t SyscallsKTest::getRet(void) const
{ return guest->getABI()->getSyscallResult(); }

bool SyscallsKTest::copyInRegMemObj(void)
{ return copyInRegMemObj(guest, kts); }

bool SyscallsKTest::copyInRegMemObj(Guest* gs, KTestStream* kts)
{
	char			*partial_reg_buf;
	void			*state_data;
	unsigned int		reg_sz;
	Arch::Arch		arch;

	reg_sz = gs->getCPUState()->getStateSize();
	if ((partial_reg_buf = kts->feedObjData(reg_sz)) == NULL)
		return false;

	state_data = gs->getCPUState()->getStateData();
	arch = gs->getArch();
	if (arch == Arch::X86_64) {
		VexGuestAMD64State	*partial_cpu, *guest_cpu;

		partial_cpu = (VexGuestAMD64State*)partial_reg_buf;
		guest_cpu = (VexGuestAMD64State*)state_data;

		/* load RAX, RCX, R11 */
		guest_cpu->guest_RAX = partial_cpu->guest_RAX;
		guest_cpu->guest_RCX = partial_cpu->guest_RCX;
		guest_cpu->guest_R11 = partial_cpu->guest_R11;
		goto done;
	}

	if (arch == Arch::ARM) {
		VexGuestARMState	*partial_cpu, *guest_cpu;

		partial_cpu = (VexGuestARMState*)partial_reg_buf;
		guest_cpu = (VexGuestARMState*)state_data;
		guest_cpu->guest_R0 = partial_cpu->guest_R0;
		goto done;
	}

	if (arch == Arch::I386) {
		VexGuestX86State	*partial_cpu, *guest_cpu;

		partial_cpu = (VexGuestX86State*)partial_reg_buf;
		guest_cpu = (VexGuestX86State*)state_data;
		guest_cpu->guest_EAX = partial_cpu->guest_EAX;
		// guest_cpu->guest_EDX = partial_cpu->guest_EDX;
		goto done;
	}

	assert (0 == 1 && "UNK ARCH");
done:
	delete [] partial_reg_buf;
	return true;
}
