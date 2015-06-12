#include <syscall.h>
#include <unistd.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "klee/Internal/Module/KModule.h"
#include "vexhelpers.h"
#include "genllvm.h"
#include "guestcpustate.h"
#include "syscall/syscallparams.h"
#include "static/Sugar.h"

#include <iostream>

#include "genllvm.h"

#include "ExeStateVex.h"
#include "KModuleVex.h"
#include "ExeChk.h"

using namespace llvm;
using namespace klee;

ExeChk::ExeChk(InterpreterHandler *ie)
: ExecutorVex(ie)
, exited(false)
{
	/* This is a really silly hack to get two genllvm's running
	 * at once. Fortunately the code doesn't change much, so
	 * we can get away with it. */
	klee_genllvm = std::move(theGenLLVM);
	klee_vexhelpers = std::move(theVexHelpers);
	assert (klee_genllvm);
	assert (klee_vexhelpers);

	vex_exe = std::unique_ptr<VexExec>(
		VexExec::create<VexExec, Guest>(gs, km_vex->getXlate()));
	assert (vex_exe != nullptr);

	jit_genllvm = std::move(theGenLLVM);
	jit_vexhelpers = std::move(theVexHelpers);
	assert(jit_genllvm);
	assert(jit_vexhelpers);

	saved_klee_cpustate = std::make_unique<char[]>(
		gs->getCPUState()->getStateSize());
	saved_jit_cpustate = std::make_unique<char[]>(
		gs->getCPUState()->getStateSize());
}

ExeChk::~ExeChk() {}

void ExeChk::saveCPU(void* s)
{
	memcpy(	s,
		gs->getCPUState()->getStateData(),
		gs->getCPUState()->getStateSize());
}

void ExeChk::loadCPU(const void* s)
{
	memcpy(	gs->getCPUState()->getStateData(),
		s,
		gs->getCPUState()->getStateSize());
}

void ExeChk::runImage(void)
{
	assert(jit_genllvm);
	assert(klee_genllvm);
	assert(!theGenLLVM);

	saveCPU(saved_jit_cpustate.get());
	theGenLLVM.swap(jit_genllvm);
	theVexHelpers.swap(jit_vexhelpers);

	vex_exe->beginStepping();

	setKLEEGen();
	ExecutorVex::runImage();
}

/* handle xfer marks the completion of a VSB */
/* KLEE runs one vexexec step ahead of VexLLVM, so we step VexLLVM here. */
/* lots of shuffling data ahead */
void ExeChk::handleXfer(ExecutionState& state, KInstruction *ki)
{
	bool		ok_step;
	Function	*cur_func = (state.stack.back()).kf->function;

	/* 1. Finish KLEE's VSB by setting up xfer to next VSB*/
	es2esv(state).updateGuestRegs();
	ExecutorVex::handleXfer(state, ki);
	saveCPU(saved_klee_cpustate.get());

	/* 2. Do VSB in VexExec's JITer */
	setJITGen();
	loadCPU(saved_jit_cpustate.get());
	ok_step = vex_exe->stepVSB();
	saveCPU(saved_jit_cpustate.get());

	/* Cross-check. Both should be in same state */
	if (memcmp(
		saved_jit_cpustate.get(),
		saved_klee_cpustate.get(),
		gs->getCPUState()->getStateSize()) != 0)
	{
		fprintf(stderr, "========JIT STATE==========\n");
		loadCPU(saved_jit_cpustate.get());
		gs->getCPUState()->print(std::cerr);

		fprintf(stderr, "========KLEE STATE=========\n");
		loadCPU(saved_klee_cpustate.get());
		gs->getCPUState()->print(std::cerr);

		cur_func->dump();
		assert (0 == 1  && "MISMATCH OOPS!");
	}

	if (exited) TERMINATE_EXIT(this, state);

	assert ((ok_step || exited) && "vex_exe failed but not exiting!?\n");

	/* Now, restore everything to KLEE state */
	loadCPU(saved_klee_cpustate.get());
	setKLEEGen();
}

/* catch syscalls so that we don't make things symbolic--
 * that would ruin the cross checking! */
void ExeChk::handleXferSyscall(ExecutionState& state, KInstruction* ki)
{
	SyscallParams   sp(gs->getSyscallParams());
	int		sys_nr;

	sys_nr = sp.getSyscall();
	switch(sys_nr) {
	case SYS_exit:
	case SYS_exit_group: {
		ObjectState	*reg_os;
		fprintf(stderr, "EXITING ON sys_nr=%d. exitcode=%d\n",
			sys_nr,
			(int)sp.getArg(0));
		reg_os = GETREGOBJ(state);
		assert (reg_os != NULL);
		state.write64(reg_os, gs->getCPUState()->getRetOff(), sp.getArg(0));
		gs->setSyscallResult(sp.getArg(0));
		exited = true;
		return;
	}
	default:
		fprintf(stderr, "BAD SYSCALL 0x%x\n", sys_nr);
		fprintf(stderr,
			"ExeChk is concrete; exit syscalls only!\n");
		gs->print(std::cerr);
		assert (0 == 1);
		break;
	}
	handleXferJmp(state, ki);
}

void ExeChk::setJITGen(void)
{
	assert (jit_genllvm);
	assert (theGenLLVM);

	theGenLLVM.swap(klee_genllvm);
	theVexHelpers.swap(klee_vexhelpers);

	theGenLLVM = std::move(jit_genllvm);
	theVexHelpers = std::move(jit_vexhelpers);

	assert (theGenLLVM);
}

void ExeChk::setKLEEGen(void)
{
	assert (klee_genllvm);
	assert (theGenLLVM);

	theGenLLVM.swap(jit_genllvm);
	theVexHelpers.swap(jit_vexhelpers);

	theGenLLVM = std::move(klee_genllvm);
	theVexHelpers = std::move(klee_vexhelpers);

	assert (theGenLLVM);
}
