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
#include "ExeChk.h"

using namespace llvm;
using namespace klee;

ExeChk::ExeChk(
	const InterpreterOptions &opts,
	InterpreterHandler *ie,
	Guest* gs)
: ExecutorVex(opts, ie, gs)
{
	/* This is a really silly hack to get two genllvm's running
	 * at once. Fortunately the code doesn't change much, so 
	 * we can get away with it. */
	klee_genllvm = theGenLLVM;
	klee_vexhelpers = theVexHelpers;
	assert (klee_genllvm);
	assert (klee_vexhelpers);

	theGenLLVM = NULL;
	theVexHelpers = NULL;

	vex_exe = VexExec::create<VexExec, Guest>(gs, xlate);
	assert (vex_exe != NULL);

	jit_genllvm = theGenLLVM;
	jit_vexhelpers = theVexHelpers;

	saved_klee_cpustate = new char[gs->getCPUState()->getStateSize()];
	saved_jit_cpustate = new char[gs->getCPUState()->getStateSize()];
}

ExeChk::~ExeChk()
{
	delete [] saved_klee_cpustate;
	delete [] saved_jit_cpustate;
	delete vex_exe;
}

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
	saveCPU(saved_jit_cpustate);
	setJITGen();
	vex_exe->beginStepping();

	setKLEEGen();
	ExecutorVex::runImage();
}

/* handle xfer marks the completion of a VSB */
/* KLEE runs one vexexec step ahead of VexLLVM, so we step VexLLVM here. */
/* lots of shuffling data ahead */
void ExeChk::handleXfer(ExecutionState& state, KInstruction *ki)
{
	bool			ok_step;

	Function	*cur_func;
	cur_func = (state.stack.back()).kf->function;

	/* finish up rest of VSB in KLEE */
	ExecutorVex::handleXfer(state, ki);
	updateGuestRegs(state);
	saveCPU(saved_klee_cpustate);

	/* do VSB in VexExec's JITer */
	setJITGen();
	loadCPU(saved_jit_cpustate);
	ok_step = vex_exe->stepVSB();
	saveCPU(saved_jit_cpustate);

	/* cross-check */
	if (memcmp(
		saved_jit_cpustate,
		saved_klee_cpustate, 
		gs->getCPUState()->getStateSize()) != 0)
	{
		fprintf(stderr, "JIT STATE\n");
		loadCPU(saved_jit_cpustate);
		gs->getCPUState()->print(std::cerr);

		fprintf(stderr, "KLEE STATE:\n");
		loadCPU(saved_klee_cpustate);
		gs->getCPUState()->print(std::cerr);

		cur_func->dump();
		assert (0 == 1  && "MISMATCH OOPS!");
	}

	/* now, restore everything */
	loadCPU(saved_klee_cpustate);
	setKLEEGen();
}

/* catch syscalls so that we don't make things symbolic-- 
 * that would ruin the cross checking! */
bool ExeChk::handleXferSyscall(ExecutionState& state, KInstruction* ki)
{
	bool		ret;
	SyscallParams   sp(gs->getSyscallParams());
	int		sys_nr;

	sys_nr = sp.getSyscall();

	switch(sys_nr) {
	case SYS_exit:
	case SYS_exit_group:
		fprintf(stderr, "EXITING ON sys_nr=%d. exitcode=%d\n", 
			sys_nr,
			(int)sp.getArg(0));
		ret = false;
		sc_ret_v(state, sp.getArg(0));
		goto done;
	default:
		fprintf(stderr, "BAD SYSCALL 0x%x\n", sys_nr);
		fprintf(stderr, 
			"ExeChk is concrete; exit syscalls only!\n");
		gs->print(std::cerr);
		assert (0 == 1);
		break;
	}

	handleXferJmp(state, ki);
	ret = true;
done:
	return ret;
}

void ExeChk::setJITGen(void)
{
	theGenLLVM = jit_genllvm;
	theVexHelpers = jit_vexhelpers;
}

void ExeChk::setKLEEGen(void)
{
	theGenLLVM = klee_genllvm;
	theVexHelpers = klee_vexhelpers;
}
