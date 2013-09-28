#include <errno.h>
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

#include "HostAccelerator.h"

#define DEBUG_HOSTACCEL(x)	x

using namespace klee;

#define OPCODE_SYSCALL 0x050f
#define is_op_syscall(x)	(((x) & 0xffff) == OPCODE_SYSCALL)

HostAccelerator::HostAccelerator(void)
: shm_id(-1)
, shm_page_c(0)
, shm_addr(NULL)
, bad_reg_c(0)
, partial_run_c(0)
, full_run_c(0)
{}

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

HostAccelerator::~HostAccelerator() { releaseSHM(); }

void HostAccelerator::releaseSHM(void)
{
	if (shm_id == -1) return;

	shmctl(shm_id, IPC_RMID, NULL);
	shmdt(shm_addr);
	shm_id = -1;
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

HostAccelerator::Status HostAccelerator::run(ExeStateVex& esv)
{
	pid_t			child_pid;
	struct shm_pkt		shmpkt;
	int			pipefd[2];
	bool			got_sc;
	int			rc, status, guest_sig;
	std::vector<ObjectPair>	objs;
	ObjectState		*regs;
	user_regs_struct	urs, urs_vex;
	user_fpregs_struct	ufprs, ufprs_vex;

	/* registers must be concrete before proceeding. ugh */
	regs = esv.getRegObj();
	if (regs->revertToConcrete() == false) {
		DEBUG_HOSTACCEL(std::cerr <<
			"[hwaccel] Failed to revert regfile.\n");
		// regs->print();
		bad_reg_c++;
		return HA_NONE;
	}

	/* XXX: should have a scheme to reuse SHMs */
	releaseSHM();

	/* setup pipe to communicate shm */
	rc = pipe(pipefd);
	assert (rc != -1 && "could not init pipe");

	/* start hw accel process in background */
	child_pid = fork();
	if (child_pid == 0) {
		close(0);
		dup(pipefd[0]);
		prctl(PR_SET_PDEATHSIG, SIGKILL);
		execlp("klee-hw", "klee-hw", NULL);	
		abort();
	}
	close(pipefd[0]);
	assert (child_pid != -1 && "Could not fork() for hw accel");

	setupSHM(esv, objs);
	writeSHM(objs);

	/* start tracing while program is sleeping on pipe */
	rc = ptrace(PTRACE_ATTACH, child_pid, NULL, NULL);
	assert (rc == 0);
	rc = waitpid(child_pid, &status, 0);
	assert (rc != -1);

	assert (WIFSTOPPED(status) && WSTOPSIG(status) == SIGSTOP);
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
		std::cerr << "[hwaccel] wtf status="
			<< (void*)((long)status) << '\n';
	}
	assert (WIFSTOPPED(status) && WSTOPSIG(status) == SIGTSTP);

	/* memory now ready for native run. save old regs, inject new regs. */

	/* collect old registers */
	rc = ptrace(PTRACE_GETREGS, child_pid, NULL, &urs);
	assert (rc == 0);
	rc = ptrace(PTRACE_GETFPREGS, child_pid, NULL, &ufprs);
	assert (rc == 0);

	/* push registers from vex state */
	VexGuestAMD64State	v;
	regs->readConcrete((uint8_t*)&v, sizeof(v));

#if DEBUG_HOSTACCEL
	/* state starts out just after a syscall, so verify it's there */
	long op = ptrace(PTRACE_PEEKDATA, child_pid, v.guest_RIP - 2, NULL);
	assert (is_op_syscall(op) && "Expected to be on syscall in VEX");
	std::cerr << "[hwaccel] Jumping to op=" << (void*)op << '\n';
#endif

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

	/* wait for run to terminate in SIGSEGV (symex) or SIGTRAP (syscall) */
	rc = waitpid(child_pid, &status, 0);
	assert (rc != -1);
	got_sc = (WSTOPSIG(status) == SIGTRAP);

	guest_sig = WSTOPSIG(status);
	DEBUG_HOSTACCEL(std::cerr <<
		"[hwaccel] Guest ran the code. Returned=" <<
		strsignal(guest_sig) << '\n');

#endif
	/* load new regs into vex state */
	rc = ptrace(PTRACE_GETREGS, child_pid, NULL, &urs_vex);
	assert (rc == 0);
	rc = ptrace(PTRACE_GETFPREGS, child_pid, NULL, &ufprs_vex);
	assert (rc == 0);

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
	DEBUG_HOSTACCEL(std::cerr <<
		"[hwaccel] old klee-hw rip=" << (void*)urs.rip << '\n');
	urs.orig_rax = -1;
	rc = ptrace(PTRACE_SETREGS, child_pid, NULL, &urs);
	assert (rc == 0);
	rc = ptrace(PTRACE_SETFPREGS, child_pid, NULL, &ufprs);
	assert (rc == 0);

	DEBUG_HOSTACCEL(std::cerr <<
		"[hwaccel] Wait for process to terminate.\n");

	/* wait for exit; data is copied in */
	ptrace(PTRACE_CONT, child_pid, NULL, NULL);
	status = 0;
	rc = waitpid(child_pid, &status, 0);
	assert (rc != -1);
	assert (WIFEXITED(status));

	/* read shm back into object states */
	readSHM(esv, objs);

	/* important to do this *after* reading the SHM so we don't copy
	 * in the old register file. Yuck! */
	PTImgAMD64::ptrace2vex(urs_vex, ufprs_vex, v);
	regs->writeConcrete((const uint8_t*)&v, sizeof(v));

	DEBUG_HOSTACCEL(std::cerr <<
		"[hwaccel] new guest rip=" << (void*)v.guest_RIP << '\n');

	assert (esv.getAddrPC() == v.guest_RIP);

	std::cerr << "[hwaccel] OK! " <<
		bad_reg_c << " / " <<
		partial_run_c << " / " <<
		full_run_c <<
		". sig=" << strsignal(guest_sig) << '\n';

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
}


void HostAccelerator::setupSHM(ExeStateVex& esv, std::vector<ObjectPair>& objs)
{
	unsigned	os_bytes = 0, header_bytes = 0;
	uint64_t	next_page = 0;

	/* collect all concrete object states and compute total space needed */
	foreach (it, esv.addressSpace.begin(), esv.addressSpace.end()) {
		const MemoryObject	*mo_c(it->first);
		const ObjectState	*os_c(it->second);

		if (mo_c->address < next_page) {
			DEBUG_HOSTACCEL(std::cerr <<
				"[hwaccel] Out of range addr=" <<
				(void*)mo_c->address << '\n');
			continue;
		}

		/* do not map in kernel pages */
		if (((uintptr_t)mo_c->address >> 32) == 0xffffffff) continue;
		/* only map page-aligned object states */
		if (((uintptr_t)mo_c->address & 0xfff)) continue;
		if (mo_c->size & 0xfff) continue;

		/* only map in concrete data */
		if (os_c->isConcrete() == false) {
			if (ObjectState::revertToConcrete(os_c) == false) {
				DEBUG_HOSTACCEL(std::cerr <<
					"[hwaccel] Skipping addr=" <<
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
	shm_page_c = (os_bytes + header_bytes + 4095)/4096;

	/* build and map shm */

	/* XXX: /proc/sys/kernel/shmmax is too small?
	 * echo 134217728 >/proc/sys/kerne/shmmax
	 */
	shm_id = shmget(
		IPC_PRIVATE,
		shm_page_c*PAGE_SIZE,
		IPC_CREAT | IPC_EXCL | SHM_R | SHM_W);
	assert (shm_id != -1);

	shm_addr = shmat(shm_id, NULL, SHM_RND);
	assert (shm_addr != (void*)-1);
	
	shm_maps = (struct hw_map_extent*)shm_addr;
	shm_payload = (void*)&shm_maps[objs.size()+1];
}
