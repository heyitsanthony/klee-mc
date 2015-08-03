#include <llvm/Support/CommandLine.h>
#include <errno.h>
#include <asm/prctl.h>
#include <assert.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include "ExeStateVex.h"
#include "static/Sugar.h"
#include "cpu/ptimgamd64.h"
#include "cpu/amd64cpustate.h"
#include "klee/Internal/Support/Timer.h"
#include <string.h>

#include "ExecutorVex.h"
#include "HostAccelerator.h"

//#define DEBUG_HOSTACCEL(x)	x
#define DEBUG_HOSTACCEL(x)
#define PTRACE_ARCH_PRCTL	((__ptrace_request)30)
#define PARANOID_SAVING

using namespace klee;

namespace
{
	llvm::cl::opt<bool> HWAccelFresh("hwaccel-fresh", llvm::cl::init(true));
}

#define OPCODE_SYSCALL 0x050f
#define is_op_syscall(x)	(((x) & 0xffff) == OPCODE_SYSCALL)

HostAccelerator::HostAccelerator(void)
: shm_id(-1)
, shm_page_c(0)
, shm_addr(NULL)
, vdso_base(NULL)
, bad_reg_c(0), partial_run_c(0), full_run_c(0), crashed_kleehw_c(0)
, badexit_kleehw_c(0)
, bad_shmget_c(0)
, xchk_ok_c(0), xchk_miss_c(0), xchk_bad_c(0)
{
	pipefd[1] = -1;
}

HostAccelerator* HostAccelerator::create(void)
{
	HostAccelerator	*h;

	h = new HostAccelerator();
#if 0
	if (shm_id == -1) {
		std::cerr << "[HostAccelerator] Failed to enable.\n";
		delete h;
		return NULL;
	}
#endif
	std::cerr << "[HostAccelerator] Enabled!\n";
	return h;
}

HostAccelerator::~HostAccelerator()
{
	killChild();
	releaseSHM();
}

void HostAccelerator::releaseSHM(void)
{
	if (shm_id == -1) return;
	int err = shmdt(shm_addr);
	if (err != 0) {
		std::cerr << "[HostAccelerator] ReleaseSHM failed: "
			  << strerror(errno) << '\n';
	}

	shm_id = -1;
	shm_page_c = 0;
	shm_addr = nullptr;
}

/* XXX: super busted */
void HostAccelerator::stepInstructions(pid_t child_pid)
{
	unsigned	c = 0;
	while (1) {
		user_regs_struct	cur_urs;
		int			rc;
		long			op;

		rc = ptrace(PTRACE_SINGLESTEP, child_pid, NULL, NULL);
		assert (rc == 0);
		wait(NULL);
		rc = ptrace(PTRACE_GETREGS, child_pid, NULL, &cur_urs);
		assert (rc == 0);
		op = ptrace(PTRACE_PEEKDATA, child_pid, cur_urs.rip, NULL);
		if (is_op_syscall(op)) break;
		c++;
	}
	std::cerr << "[hwaccel] Instruction count: " << c << '\n';
}

void HostAccelerator::setupChild(void)
{
	int	rc;

	/* setup pipe to communicate shm */
	rc = pipe(pipefd);
	assert (rc != -1 && "could not init pipe");

	DEBUG_HOSTACCEL(std::cerr << "[hwaccel] creating new child\n");

	/* start hw accel process in background */
	child_pid = fork();
	if (child_pid == 0) {
		close(0); /* no stdin */
		dup(pipefd[0]);
		prctl(PR_SET_PDEATHSIG, SIGKILL);

		/* this is probably slower than it needs to be... */
		ptrace(PTRACE_TRACEME, 0, 0, 0);
		raise(SIGSTOP);

		execlp("klee-hw", "klee-hw", NULL);
		abort();
	}
	close(pipefd[0]);
	assert (child_pid != -1 && "Could not fork() for hw accel");

	DEBUG_HOSTACCEL(std::cerr<<"[hwaccel] child pid="<<child_pid<<'\n');
}

void HostAccelerator::killChild(void)
{
	if (pipefd[1] == -1) return;
	close(pipefd[1]);

	if (child_pid != -1 && !HWAccelFresh) {
		int status;
		ptrace(PTRACE_DETACH, child_pid, 0, 0);
		kill(child_pid, SIGKILL);
		waitpid(child_pid, &status, 0);
	}
	child_pid = -1;
	pipefd[1] = -1;
}

HostAccelerator::Status HostAccelerator::run(ExeStateVex& esv)
{
	struct shm_pkt		shmpkt;
	bool			got_sc, ok;
	int			rc, status, last_status;
	void			*old_fs, *new_fs;
	std::vector<ObjectPair>	objs;
	ObjectState		*regs;
	user_regs_struct	urs, urs_vex;
	user_fpregs_struct	ufprs, ufprs_vex;

	/* find vdso so we can unmap it later */
	/* TODO: no longer need to do this post KLEE-VDSO */
	if (vdso_base == NULL) {
		for (auto &p : esv.addressSpace){
			if (p.first->name == "[vdso]") {
				vdso_base = (void*)p.first->address;
				break;
			}
		}
	}

	/* registers must be concrete before proceeding. ugh */
	regs = esv.getRegObj();
	if (regs->revertToConcrete() == false) {
		DEBUG_HOSTACCEL(std::cerr <<
			"[hwaccel] Failed to revert regfile.\n");
		bad_reg_c++;
		return HA_NONE;
	}

	if (HWAccelFresh) killChild();
	if (pipefd[1] == -1) setupChild();

	if (setupSHM(esv, objs) == false) {
		DEBUG_HOSTACCEL(std::cerr <<
			"[hwaccel] Failed to allocate SHM.\n");
		bad_shmget_c++;
		return HA_NONE;
	}
	writeSHM(objs);

	/* start tracing while program is sleeping on pipe */
	/* XXX: we don't start tracing on pipe-- we start tracing
	 * on exec now */
#if 0
	rc = ptrace(PTRACE_ATTACH, child_pid, NULL, NULL);
	if (rc != 0) {
		DEBUG_HOSTACCEL(std::cerr
			<< "[hwaccel] first attach failed\n" << '\n');
		kill(child_pid, SIGKILL);
		waitpid(child_pid, &status, 0);
		killChild();
		setupChild();
		rc = ptrace(PTRACE_ATTACH, child_pid, NULL, NULL);
		assert (rc == 0);
	}
#endif
	rc = waitpid(child_pid, &status, 0);
	assert (rc != -1);

	DEBUG_HOSTACCEL(
		std::cerr << "[hwaccel] first childpid status="
		<< (void*)(long)status  << '\n');

	/* SIGTRAP is if the execve() isn't done by the time we ptrace */
	if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP) {
		/* send first CONT to ack SIGTRAP */
		DEBUG_HOSTACCEL(std::cerr
			<< "[hwaccel] ack SIGTRAP\n" << '\n');
		ptrace(PTRACE_CONT, child_pid, NULL, NULL);
		waitpid(child_pid, &status, 0);
		last_status = status;
		/* next CONT is to set process running */
	} else {
		ok = WIFSTOPPED(status) && WSTOPSIG(status) == SIGSTOP;
		if (!ok) std::cerr << "[hwaccel] bad status: " <<
				(void*)(long)status << '\n';
		assert (ok);
		ptrace(PTRACE_CONT, child_pid, NULL, NULL);
		rc = waitpid(child_pid, &status, 0);
		assert (rc != -1);
		ok = WIFSTOPPED(status) && WSTOPSIG(status) == SIGTRAP;
		last_status = status;
		assert (ok);
	}

	ptrace(PTRACE_CONT, child_pid, NULL, NULL);

	/* alert child with pipe */
	shmpkt.sp_shmid = shm_id;
	shmpkt.sp_pages = shm_page_c;
	rc = write(pipefd[1], &shmpkt, sizeof(shmpkt));
	assert (rc == sizeof(shmpkt));

	/* wait for raise(SIGSTOP) from klee-hw once shm memory is loaded*/
	rc = waitpid(child_pid, &status, 0);
	assert (rc != -1);
	if (!(WIFSTOPPED(status) && WSTOPSIG(status) == SIGTSTP)) {
		std::cerr << "[hwaccel] Expected loaded shm SIGTSTP. "
			"Weird status=" << (void*)((long)status) <<
			". Last status=" << (void*)(long)last_status << '\n';

		ptrace(PTRACE_DETACH, child_pid, 0, 0);
		kill(child_pid, SIGKILL);
		waitpid(child_pid, &status, 0);

		killChild();
		if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGSEGV)
			crashed_kleehw_c++;

		return HA_NONE;
	}

	/* memory now ready for native run. save old regs, inject new regs. */

	/* collect old registers */
	rc = ptrace(PTRACE_GETREGS, child_pid, NULL, &urs);
	assert (rc == 0);

#ifdef PARANOID_SAVING
	rc = ptrace(PTRACE_GETFPREGS, child_pid, NULL, &ufprs);
	assert (rc == 0);
	rc = ptrace(PTRACE_ARCH_PRCTL, child_pid, &old_fs, ARCH_GET_FS);
	assert (rc == 0);
#endif

	/* push registers from vex state */
	VexGuestAMD64State	v;
	regs->readConcrete((uint8_t*)&v, sizeof(v));

	new_fs = (void*)v.guest_FS_ZERO;
	rc = ptrace(PTRACE_ARCH_PRCTL, child_pid, new_fs, ARCH_SET_FS);
	assert (rc == 0);

#if DEBUG_HOSTACCEL
	/* state starts out just after a syscall, so verify it's there */
	long op = ptrace(PTRACE_PEEKDATA, child_pid, v.guest_RIP - 2, NULL);
	assert (is_op_syscall(op) && "Expected to be on syscall in VEX");
	std::cerr << "[hwaccel] Jumping to op=" << (void*)op << '\n';
#endif

	memset(&urs_vex, 0, sizeof(urs_vex));
	PTImgAMD64::vex2ptrace(v, urs_vex, ufprs_vex);
	urs_vex.cs = urs.cs;
	urs_vex.ss = urs.ss;

	rc = ptrace(PTRACE_SETREGS, child_pid, NULL, &urs_vex);
	assert (rc == 0);
	rc = ptrace(PTRACE_SETFPREGS, child_pid, NULL, &ufprs_vex);
	assert (rc == 0);

	/* wait for next syscall or signal, whichever comes first */
#if 0
	stepInstructions(child_pid);
#else
	rc = ptrace(PTRACE_SYSCALL, child_pid, NULL, NULL);
	assert (rc == 0);

	/* wait for run to terminate in SIGSEGV (symex) or SIGTRAP (syscall) */
	rc = waitpid(child_pid, &status, 0);
	assert (rc != -1);
	got_sc = (WSTOPSIG(status) == SIGTRAP);

	DEBUG_HOSTACCEL(std::cerr <<
		"[hwaccel] Guest ran the code. Returned=" <<
		strsignal(WSTOPSIG(status)) << '\n');

#endif
	/* load new regs into vex state */
	rc = ptrace(PTRACE_GETREGS, child_pid, NULL, &urs_vex);
	assert (rc == 0);
	rc = ptrace(PTRACE_GETFPREGS, child_pid, NULL, &ufprs_vex);
	assert (rc == 0);

	/* XXX: I've observed that ever so occasionally, the system
	 * returns '0' here */
	if (urs_vex.rip < 1000) {
		std::cerr << "[hwaccel] Bogus RIP? " << urs_vex.rip << '\n';
		badexit_kleehw_c++;
		killChild();
		return HA_CRASHED;
	}

	if (got_sc) {
		/* if on SC, rip is set to *after* syscall, so back up */
		urs_vex.rax = urs_vex.orig_rax;
		urs_vex.rip -= 2;
		full_run_c++;
	} else {
		partial_run_c++;
	}

#if DEBUG_HOSTACCEL
	op = ptrace(PTRACE_PEEKDATA, child_pid, urs_vex.rip, NULL);
	std::cerr << "[hwaccel] New guest on op=" << (void*)op << '\n';
	assert (!got_sc || is_op_syscall(op));
#endif

	/* restore old regs so child can copy into shm */
	urs.orig_rax = -1;
	rc = ptrace(PTRACE_SETREGS, child_pid, NULL, &urs);
	assert (rc == 0);

#ifdef PARANOID_SAVING
	rc = ptrace(PTRACE_SETFPREGS, child_pid, NULL, &ufprs);
	assert (rc == 0);
	rc = ptrace(PTRACE_ARCH_PRCTL, child_pid, old_fs, ARCH_SET_FS);
	assert (rc == 0);
#endif

	DEBUG_HOSTACCEL(std::cerr <<
		"[hwaccel] Wait for process to terminate.\n");

	/* wait for exit; data is copied in */
	rc = ptrace(PTRACE_CONT, child_pid, NULL, NULL);
	assert (rc == 0);

	status = 0;
	rc = waitpid(child_pid, &status, 0);
	assert (rc != -1);

	/* bad termination? */
	if (WIFEXITED(status) && HWAccelFresh) {
		/* OK */
	} else if (!(WIFSTOPPED(status) && WSTOPSIG(status) == SIGTSTP)) {
 		/* expects sigtstp */
		badexit_kleehw_c++;
		killChild();
		return HA_CRASHED;
	} else {
		badexit_kleehw_c++;
		child_pid = -1;
		killChild();
		return HA_CRASHED;
	}

	ptrace(PTRACE_DETACH, child_pid, NULL, NULL);

	/* read shm back into object states */
	readSHM(esv, objs);

	/* important to do this *after* reading the SHM so we don't copy
	 * in the old register file. Yuck! */
	PTImgAMD64::ptrace2vex(urs_vex, ufprs_vex, v);
	regs->writeConcrete((const uint8_t*)&v, sizeof(v));

	DEBUG_HOSTACCEL(std::cerr <<
		"[hwaccel] new guest rip=" << (void*)v.guest_RIP << '\n');

	assert (esv.getAddrPC() == v.guest_RIP);

#if 0
	std::cerr << "[hwaccel] OK! " <<
		"bad=" << bad_reg_c << " / "
		"crash=" << crashed_kleehw_c << " / "
		"part=" << partial_run_c << " / "
		"full=" << full_run_c <<
		". sig=" << strsignal(guest_sig) << '\n';
#endif
	return (got_sc) ? HA_SYSCALL : HA_PARTIAL;
}


void HostAccelerator::readSHM(
	ExeStateVex& s,
	const std::vector<ObjectPair>& objs)
{
	unsigned	bw = 0;

	for (unsigned i = 0; i < objs.size(); i++) {
		ObjectState	*os;
		uint8_t		*p(((uint8_t*)shm_payload)+bw);

		/* check if change before committing to an objectstate fork */
		if (objs[i].second->cmpConcrete(p, objs[i].first->size) == 0) {
			bw += objs[i].first->size;
			continue;
		}

		os = s.addressSpace.findWriteableObject(objs[i].first);
		os->writeConcrete(p, objs[i].first->size);
		bw += objs[i].first->size;
	}
}


void HostAccelerator::writeSHM(const std::vector<ObjectPair>& objs)
{
	unsigned bw = 0;

	/* create map info table and write-out object states */
	for (unsigned i = 0; i < objs.size(); i++) {
		const MemoryObject	*mo(objs[i].first);
		const ObjectState	*os(objs[i].second);

		shm_maps[i].hwm_addr = (void*)mo->address;
		shm_maps[i].hwm_bytes = mo->size;
		shm_maps[i].hwm_prot = 0xdeadbeef; /* XXX FIXME */
		os->readConcrete(((uint8_t*)shm_payload) + bw, mo->size);
		bw += mo->size;
	}

	/* terminator */
	shm_maps[objs.size()].hwm_addr = NULL;
	shm_maps[objs.size()].hwm_prot = (HWAccelFresh) ? 1 : 0;
}


bool HostAccelerator::setupSHM(ExeStateVex& esv, std::vector<ObjectPair>& objs)
{
	unsigned	os_bytes = 0, header_bytes = 0;
	uint64_t	next_page = 0;
	unsigned	new_page_c;

	/* collect all concrete object states and compute total space needed */
	for (auto &p : esv.addressSpace) {
		const MemoryObject	*mo_c(p.first);
		const ObjectState	*os_c(p.second);

		if (mo_c->address < next_page) {
			DEBUG_HOSTACCEL(std::cerr <<
				"[hwaccel] Out of range addr=" <<
				(void*)mo_c->address << '\n');
			continue;
		}

		/* inhibit vdso so kernel page is not accessed */
		if ((void*)mo_c->address == vdso_base) continue;

		/* do not map in kernel pages */
		if (((uintptr_t)mo_c->address >> 32) == 0xffffffff) continue;
		/* only map page-aligned object states */
		if (((uintptr_t)mo_c->address & 0xfff)) continue;
		if (mo_c->size & 0xfff) continue;

		/* only map in concrete data */
		if (os_c->isConcrete() == false) {
			if (ObjectState::revertToConcrete(os_c) == false) {
				DEBUG_HOSTACCEL(std::cerr <<
					"[hwaccel] Skipping symbolic addr=" <<
					(void*)mo_c->address << '\n');
				// os_c->print();
				next_page = 4096*((mo_c->address + 4095)/4096);
				continue;
			}

			assert (os_c->isConcrete());
		}

		objs.push_back(ObjectPair(mo_c, os_c));
		os_bytes += mo_c->size;
	}

	header_bytes = (objs.size()+1) * sizeof(struct hw_map_extent);
	new_page_c = (os_bytes + header_bytes + 4095)/4096;

	if (new_page_c < shm_page_c) goto done;

	releaseSHM();

	/* build and map shm */
	/* XXX: /proc/sys/kernel/shmmax is too small?
	 * echo 134217728 >/proc/sys/kerne/shmmax */
	shm_id = shmget(
		IPC_PRIVATE,
		new_page_c * PAGE_SIZE,
		IPC_CREAT | IPC_EXCL | SHM_R | SHM_W);
	if (shm_id == -1) {
		return false;
	}

	shm_page_c = new_page_c;
	shm_addr = shmat(shm_id, NULL, SHM_RND);
	assert (shm_addr != (void*)-1);

	/* this is so the shm is not persistent;
	 * otherwise it stays if the program exits without a shmdt()! */
	shmctl(shm_id, IPC_RMID, NULL);

	shm_maps = (struct hw_map_extent*)shm_addr;

done:
	shm_payload = (void*)&shm_maps[objs.size()+1];
	return true;
}


bool HostAccelerator::xchk(const ExecutionState& es_hw, ExecutionState& es_klee)
{
	bool	ret = true;

	/* ptrace syscall clobbers rcx, r11, etc */
	fixupHWShadow(es_hw, es_klee);

	std::cerr << "[HWAccelXChk] Instructions: " <<
		es_klee.personalInsts << '\n';
	for (auto &p : es_klee.addressSpace) {
		const MemoryObject	*mo(p.first);
		const ObjectState	*os(p.second), *os2;

		os2 = es_hw.addressSpace.findObject(mo);
		assert (os2 != NULL);
		if (os->cmpConcrete(*os2) == 0)
			continue;

		std::cerr << "OOPS ON: " << (void*)mo->address << '\n';
		std::cerr << "=====Interpreter vs Hardware====\n";
		os->printDiff(*os2);
		if (os2->getSize() == AMD64CPUState::REGFILE_BYTES) {
			const VexGuestAMD64State	*v;

#define get_amd64(x)	\
	(const VexGuestAMD64State*)((const void*)(x)->getConcreteBuf())
			std::cerr << "Interpreter RegDump:\n";
			v = get_amd64(os);
			AMD64CPUState::print(std::cerr, *v);
			std::cerr << "Guest RegDump:\n";
			v = get_amd64(os2);
			AMD64CPUState::print(std::cerr, *v);
		}
		std::cerr << "=========================\n";
		ret = false;
	}

	if (ret) {
		std::cerr << "[HWAccelXChk] OK.\n";
		xchk_ok_c++;
	} else
		xchk_bad_c++;

	return ret;
}

void HostAccelerator::fixupHWShadow(
	const ExecutionState& hw, ExecutionState& shadow)
{
	VexGuestAMD64State	*v;
	ref<Expr>		new_pc_e;
	uint64_t		rflags;
	uint64_t		x_reg(~0ULL);

	/* rflags is jiggled a bit in ptrace->vex, so replicate behavior */
	v = (VexGuestAMD64State*)GETREGOBJ(shadow)->getConcreteBuf();
	rflags = AMD64CPUState::getRFLAGS(*v);
	v->guest_DFLAG = (rflags & (1 << 10)) ? -1 :1;
	v->guest_CC_OP = 0 /* AMD64G_CC_OP_COPY */;
	v->guest_CC_DEP1 = rflags & (0xff | (3 << 10));
	v->guest_CC_DEP2 = 0;
	v->guest_CC_NDEP = v->guest_CC_DEP1;

	/* rcx and r11 are clobbered, so use cpu results */
	AS_COPY2(hw, &x_reg, VexGuestAMD64State, guest_RCX, 8);
	AS_COPYOUT(shadow, &x_reg, VexGuestAMD64State, guest_RCX, 8);
	AS_COPY2(hw, &x_reg, VexGuestAMD64State, guest_R11, 8);
	AS_COPYOUT(shadow, &x_reg, VexGuestAMD64State, guest_R11, 8);

	/* ymm16 is intermediate value but dead by now */
	char	ymm16[32];
	AS_COPY2(hw, &ymm16, VexGuestAMD64State, guest_YMM16, 32);
	AS_COPYOUT(shadow, &ymm16, VexGuestAMD64State, guest_YMM16, 32);

	/* shadow state will return *after* syscall (since it's on sc_enter);
	 * accel returns *at* syscall (since it's abotu to call sc_enter) */
	v->guest_RIP -= 2;

	/* VEX will generate EMNOTEs but not hardware! */
	x_reg = 0;
	AS_COPY2(hw, &x_reg, VexGuestAMD64State, guest_EMNOTE, 4);
	v->guest_EMNOTE = x_reg;
}
