#include <sys/resource.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <syscall.h>
#include <string.h>
#include <stdio.h>

#include "guestcpustate.h"
#include "klee/Internal/Module/KModule.h"
#include "ExecutorVex.h"
#include "SymSyscalls.h"

extern "C" 
{
#include "valgrind/libvex_guest_amd64.h"
}

using namespace klee;

SymSyscalls::SymSyscalls(ExecutorVex* owner)
: exe_vex(owner)
, sc_dispatched(0)
, sc_retired(0)
{
	memset(syscall_c, 0, sizeof(syscall_c));
}

ObjectState* SymSyscalls::makeSCRegsSymbolic(ExecutionState& state)
{
	Guest			*gs;
	MemoryObject		*cpu_mo;
	ObjectState		*state_regctx_os;

	const MemoryObject	*old_mo;
	uint64_t		old_mo_addr;
	const ObjectState	*old_regctx_os;
	unsigned int		sz;

	gs = exe_vex->getGuest();
	sz = gs->getCPUState()->getStateSize();

	old_mo = state.getRegCtx();
	assert (old_mo && "No register context ever set?");

	old_regctx_os = state.addressSpace.findObject(old_mo);
	// do not unbind the object, we need to be able to grab it
	// for replay
	// state.unbindObject(old_mo);

	/* 1. make all of symbolic */
	cpu_mo = exe_vex->allocRegCtx(&state);
	state_regctx_os = exe_vex->executeMakeSymbolic(state, cpu_mo, "reg");

	/* 2. set everything that should be initialized */
	const char*  state_data = (const char*)gs->getCPUState()->getStateData();
	for (unsigned int i=0; i < sz; i++) {
		unsigned int	reg_idx;

		reg_idx = i/8;
		if (	reg_idx == offsetof(VexGuestAMD64State, guest_RAX)/8 ||
			reg_idx == offsetof(VexGuestAMD64State, guest_RCX)/8 ||
			reg_idx == offsetof(VexGuestAMD64State, guest_R11)/8)
		{
			/* ignore rax, rcx, r11 */
			assert (state_regctx_os->isByteConcrete(i) == false);
			continue;
		}

		/* copy it by expression */
		state.write(
			state_regctx_os,
			i,
			state.read8(old_regctx_os, i));
	}

	/* make state point to right register context on xfer */
	state.setRegCtx(cpu_mo);

	return state_regctx_os;
}

ObjectState* SymSyscalls::sc_ret_ge0(ExecutionState& state)
{
	ObjectState		*state_regctx_os;
	bool			constrained;
	ref<Expr>		success_constraint;
	ref<Expr>		rax_expr;

	state_regctx_os = makeSCRegsSymbolic(state);

	/* 3. force zero <= sysret; sysret >= 0; no errors */
	rax_expr = state.read(
			state_regctx_os,
			offsetof(VexGuestAMD64State, guest_RAX),
			64);
	success_constraint = SleExpr::create(
		ConstantExpr::create(0, 64),
		rax_expr);

	state.constraints.print(std::cerr);
	constrained = exe_vex->addConstraint(state, success_constraint);
	assert (constrained); // newly symbolic, should always succeed..
	
	return state_regctx_os;
}

ObjectState* SymSyscalls::sc_ret_le0(ExecutionState& state)
{
	ObjectState		*state_regctx_os;
	bool			constrained;
	ref<Expr>		success_constraint;

	state_regctx_os = makeSCRegsSymbolic(state);

	/* 3. force sysret <= 0 */
	success_constraint = SleExpr::create(
		state.read(
			state_regctx_os,
			offsetof(VexGuestAMD64State, guest_RAX),
			64),
		ConstantExpr::create(0, 64));

	constrained = exe_vex->addConstraint(state, success_constraint);
	assert (constrained); // newly symbolic, should always succeed..

	return state_regctx_os;
}


ObjectState* SymSyscalls::sc_ret_or(
	ExecutionState& state, uint64_t o1, uint64_t o2)
{
	ObjectState		*state_regctx_os;
	bool			constrained;
	ref<Expr>		success_constraint;

	state_regctx_os = makeSCRegsSymbolic(state);

	success_constraint = OrExpr::create(
		EqExpr::create(
			ConstantExpr::create(o1, 64),
			state.read(
				state_regctx_os,
				offsetof(VexGuestAMD64State, guest_RAX),
				64)),
		EqExpr::create(
			ConstantExpr::create(o2, 64),
			state.read(
				state_regctx_os,
				offsetof(VexGuestAMD64State, guest_RAX),
				64)));

	constrained = exe_vex->addConstraint(state, success_constraint);
	assert (constrained); // symbolic, should always succeed..

	return state_regctx_os;
}

ObjectState* SymSyscalls::sc_ret_range(
	ExecutionState& state,
	uint64_t lo, uint64_t hi)
{
	ObjectState		*state_regctx_os;
	bool			constrained;
	ref<Expr>		success_constraint;

	state_regctx_os = makeSCRegsSymbolic(state);

	success_constraint = SleExpr::create(
		ConstantExpr::create(lo, 64),
		state.read(
			state_regctx_os,
			offsetof(VexGuestAMD64State, guest_RAX),
			64));

	constrained = exe_vex->addConstraint(state, success_constraint);
	assert (constrained); // symbolic, should always succeed..

	success_constraint = SgeExpr::create(
		ConstantExpr::create(hi, 64),
		state.read(
			state_regctx_os,
			offsetof(VexGuestAMD64State, guest_RAX),
			64));

	constrained = exe_vex->addConstraint(state, success_constraint);
	assert (constrained); // symbolic, should always succeed..

	return state_regctx_os;
}

void SymSyscalls::sc_munmap(
	ExecutionState& state, 
	const SyscallParams &sp)
{
	uint64_t		addr;
	uint64_t		len;
	const MemoryObject	*mo;

	addr = sp.getArg(0);
	len = sp.getArg(1);
	mo = state.addressSpace.resolveOneMO(addr);
	if (mo == NULL) {
		exe_vex->terminateStateOnError(
			state,
			"munmap error: munmapping bad address",
			"munmap.err");
		return;
	}

	assert (mo->size == len &&
		mo->address == addr && "UNHANDLED BAD SIZE");

	state.unbindObject(mo);
	sc_ret_le0(state);
}

void SymSyscalls::sc_fail(ExecutionState& state)
{
	sc_ret_v(state, -1);
}

void SymSyscalls::sc_stat(ExecutionState& state, const SyscallParams &sp)
{
	uint64_t		statbuf_addr;

	statbuf_addr = sp.getArg(1);
	exe_vex->makeRangeSymbolic(
		state,
		(void*)statbuf_addr,
		sizeof(struct stat),
		"statbuf");

	/* fail or success */
	sc_ret_le0(state);
}

void SymSyscalls::sc_pread(ExecutionState& state, const SyscallParams& sp)
{
	uint64_t		buf_sz, buf_addr;

	buf_addr = sp.getArg(1);
	buf_sz = sp.getArg(2);

	fprintf(stderr, "MARK %p-%p SYMBOLIC\n", buf_addr, buf_addr+buf_sz);
	exe_vex->makeRangeSymbolic(state, (void*)buf_addr, buf_sz, "readbuf");
	if (rand() % 2) sc_ret_v(state, buf_sz);
	else		sc_ret_v(state, -1);
}

void SymSyscalls::sc_read(ExecutionState& state, const SyscallParams& sp)
{
	uint64_t		buf_sz, buf_addr;

	buf_addr = sp.getArg(1);
	buf_sz = sp.getArg(2);

	fprintf(stderr, "MARK %p-%p SYMBOLIC\n", buf_addr, buf_addr+buf_sz);
	exe_vex->makeRangeSymbolic(state, (void*)buf_addr, buf_sz, "readbuf");
	if (rand() % 2) sc_ret_v(state, buf_sz);
	else		sc_ret_v(state, -1);
}

void SymSyscalls::sc_getdents(ExecutionState& state, const SyscallParams& sp)
{
	uint64_t		buf_sz, buf_addr;

	buf_addr = sp.getArg(1);
	buf_sz = sp.getArg(2);

	fprintf(stderr, "MARK %p-%p SYMBOLIC\n", buf_addr, buf_addr+buf_sz);
	exe_vex->makeRangeSymbolic(state, (void*)buf_addr, buf_sz, "getdents");

	if (rand() % 2)
		sc_ret_v(state, buf_sz);
	else
		sc_ret_v(state, -1);
}

void SymSyscalls::sc_getcwd(ExecutionState& state, const SyscallParams& sp)
{
	uint64_t		buf_sz, buf_addr;
	ObjectState		*os_cwdbuf;
	const MemoryObject	*mo_cwdbuf;

	buf_addr = sp.getArg(0);
	buf_sz = sp.getArg(1);
	mo_cwdbuf = state.addressSpace.resolveOneMO(buf_addr);
	state.unbindObject(mo_cwdbuf);
	
	/* return symbolic path name */
	os_cwdbuf = exe_vex->executeMakeSymbolic(
		state,
		mo_cwdbuf,
		ConstantExpr::alloc(buf_sz, 32),
		"cwdbuf");

	/* ensure buffer is null-terminated */
	state.write8(
		os_cwdbuf, 
		(buf_addr - mo_cwdbuf->address) + (buf_sz-1),
		0);

	/* TODO: simulate errors */
	sc_ret_v(state, buf_addr);
}

void SymSyscalls::sc_mmap(
	ExecutionState& state,
	const SyscallParams& sp, KInstruction* ki)
{
	MemoryObject		*new_mo;
	ObjectState		*new_os;
	uint64_t		addr = sp.getArg(0);
	uint64_t		length = sp.getArg(1);
	uint64_t		prot = sp.getArg(2);
#if 0
/* Will be useful sometime? */
	uint64_t		flags = sp.getArg(3);
	uint64_t		fd = sp.getArg(4);
	uint64_t		offset = sp.getArg(5);
#endif

	if (addr == 0) {
		/* not requesting a specific address */
		new_mo = exe_vex->getMM()->allocate(
			length, 
			true /* local */,
			false /* global */, 
			ki->inst,
			&state);
		if (new_mo) addr = new_mo->address;
	} else {
		/* requesting an address */
		new_mo = exe_vex->addExternalObject(
			state, (void*)addr, length,
			((prot & PROT_WRITE) == 0) /* isReadOnly */);
	}

	if (new_mo == NULL) {
		/* Couldn't create memory object, 
		 * most likely because size was too big */
		sc_fail(state);
		return;
	}

	/* returned data will be symbolic */
	/* XXX: or should it be marked as zero? */
	new_mo->setName("mmaped");
	new_os = exe_vex->executeMakeSymbolic(state, new_mo, "mmap");
	assert (new_os != NULL && "Could not make object state");

	if ((prot & PROT_WRITE) == 0)
		new_os->setReadOnly(true);

	/* always succeed */
	/* TODO: should we enable failures here? */
	sc_ret_v(state, addr);

	fprintf(stderr, "MMAP ADDR=%p-%p\n", addr, addr + length);
	/* note: register context is not made symbolic here */
}

void SymSyscalls::sc_ret_v(ExecutionState& state, uint64_t v)
{
	ObjectState* state_regctx_os;
	state_regctx_os = state.addressSpace.findObject(state.getRegCtx());
	state.write64(
		state_regctx_os, 
		offsetof(VexGuestAMD64State, guest_RAX),
		v);
}

void SymSyscalls::sc_writev(ExecutionState& state)
{
	/* doesn't affect proces state */
	sc_ret_ge0(state);
}

bool SymSyscalls::apply(
	ExecutionState& state,
	KInstruction* ki,
	const SyscallParams& sp)
{
	bool		ret = true;
	int		sys_nr;

	sys_nr = sp.getSyscall();

	assert (sys_nr < 512 && "SYSCALL OVERFLOW!!");
	syscall_c[sys_nr]++;
	fprintf(stderr, "SYSCALL %d (%d total)!\n",
		sys_nr, syscall_c[sys_nr]);

	sc_dispatched++;
	switch(sys_nr) {
	case SYS_newfstatat: /* for du */
		sc_ret_or(state, 0, -1);
		exe_vex->makeRangeSymbolic(
			state,
			(void*)sp.getArg(2),
			sizeof(struct stat),
			"newstatbuf");
		break;
	case SYS_sync:
		break;
	case SYS_umask:
		sc_ret_v(state, 0666);
		break;
	case SYS_rt_sigaction:
		/* XXX need to make old action struct symbolic */
	case SYS_rt_sigprocmask:
		sc_ret_v(state, 0);
		break;
	case SYS_access:
		sc_ret_range(state, -1, 0);
		break;
	case SYS_lseek:
		sc_ret_range(state, -1, 4096); /* 4096 byte file! */
		break;
	case SYS_fcntl:
		/* this is really complicated, {-1, 0, 1} should be OK. */
		sc_ret_range(state, -1, 1);
		break;
	case SYS_sched_setaffinity:
	case SYS_sched_getaffinity:
		sc_fail(state);
		break;
	case SYS_fadvise64:
		sc_ret_v(state, 0);
		break;
	case SYS_pread64:
		sc_pread(state, sp);
		break;
	case SYS_read:
		sc_read(state, sp);
		break;
	case SYS_open:
		/* what kind of checks do we care about here? */
		sc_ret_ge0(state);
		break;
	case SYS_futex:
		sc_ret_range(state, -1, 1);
		break;
	case SYS_tgkill:
		sc_ret_or(state, -1, 0);
		break;
	case SYS_getgid:
	case SYS_getuid:
		sc_ret_ge0(state);
		break;
	case SYS_getpid:
	case SYS_gettid:
		sc_ret_v(state, 1000); /* FIXME: single threaded*/
		break;
	case SYS_setgid:
	case SYS_setuid:
		sc_ret_or(state, -1, 0);
		break;
	case SYS_geteuid:
	case SYS_getegid:
		sc_ret_range(state, 0, 1);
		break;
	case SYS_write:
		/* just return the length, later return -1 too? */
		/* ret >= 0 IS BAD NEWS. -- don't do it or you'll loop */
	//	sc_ret_v(state, sp.getArg(2));
#if 0	
		fprintf(stderr, ">>>>>>>>>>>>>WRITING %d bytes (0=%d, 1=%p)\n",
			sp.getArg(2), sp.getArg(0), sp.getArg(1));
		if ((0x00000f00000 & sp.getArg(1)) == 0x400000) {
			for (int i = 0; i < sp.getArg(2); i++)
				fprintf(stderr, "[%d]=%c\n",
					i,
					((char*)sp.getArg(1))[i]);
		}
#endif
		if (rand() % 2)
			sc_ret_or(state, -1, sp.getArg(2));
		else
//			sc_ret_or(state, sp.getArg(2), -1);
			sc_ret_v(state, -1);

		break;
	case SYS_getgroups:
		exe_vex->makeRangeSymbolic(
			state, (void*)sp.getArg(1), sp.getArg(0), "getgroups");
		sc_ret_range(state, -1, 2);
		break;

	case SYS_writev:
		// fd, iov, iovcnt
		sc_ret_ge0(state);
		break;
	case SYS_uname:
		/* ugh, handle later */
		fprintf(stderr, "WARNING: failing uname\n");
		sc_ret_v(state, -1);
		break;
	case SYS_close:
		sc_ret_v(state, 0);
		break;
	case SYS_fstat:
	/* there's a distinction between these two based on whether
	 * the path is a symlink. should we care? */
	case SYS_lstat:
	case SYS_stat:
		sc_stat(state, sp);
		break;
	case SYS_getcwd:
		sc_getcwd(state, sp);
		break;
	case SYS_brk:
		// always fail this, just like in pt_run
		sc_fail(state);
		break;
	case SYS_ioctl:
		sc_ret_ge0(state);
		break;
	case SYS_mremap:
		sc_fail(state);
		break;
	case SYS_munmap:
		sc_munmap(state, sp);
		break;
	case SYS_mmap:
		sc_mmap(state, sp, ki);
		break;
	case SYS_dup:
		sc_ret_ge0(state);
		break;
	case SYS_getrlimit:
		exe_vex->makeRangeSymbolic(
			state,
			(void*)sp.getArg(1),
			sizeof(struct rlimit),
			"getrlimit");
		sc_ret_v(state, 0);
		break;
	case SYS_getrusage:
		exe_vex->makeRangeSymbolic(
			state,
			(void*)sp.getArg(1),
			sizeof(struct rusage),
			"getrusage");

		sc_ret_v(state, 0);
		break;
	case SYS_getdents:
		sc_getdents(state, sp);
		break;
	case SYS_unlink:
		sc_ret_v(state, 0);
		break;
	case SYS_exit:
	case SYS_exit_group:
		fprintf(stderr, "EXITING ON sys_nr=%d. exitcode=%d\n", 
			sys_nr,
			sp.getArg(0));
		ret = false;
		break;
	case SYS_fchmod:
	case SYS_fchown:
		sc_ret_v(state, 0);
		break;
	case SYS_utimensat:
		sc_ret_v(state, 0);
		break;
	default:
		fprintf(stderr, "UNKNOWN SYSCALL 0x%x\n", sys_nr);
		exe_vex->getGuest()->print(std::cerr);
		assert (0 == 1);
		break;
	}

	sc_retired++;

	return ret;
}
