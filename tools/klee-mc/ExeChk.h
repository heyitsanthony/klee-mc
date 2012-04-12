#ifndef EXECHK_H
#define EXECHK_H

#include "ExecutorVex.h"
#include "vexexec.h"

class GenLLVM;
class VexHelpers;

namespace klee {

class ExeChk : public ExecutorVex
{
public:
	ExeChk(InterpreterHandler *ie);
	virtual ~ExeChk(void);

	virtual void runImage(void);
protected:
	virtual void handleXferSyscall(ExecutionState& state, KInstruction* ki);

	virtual void handleXfer(ExecutionState& state, KInstruction *ki);
private:
	void saveCPU(void*);
	void loadCPU(const void*);
	void setJITGen(void);
	void setKLEEGen(void);
	VexExec		*vex_exe;

	/* we have to do a lot of state swapping to get this to work */
	GenLLVM		*klee_genllvm;
	VexHelpers	*klee_vexhelpers;

	GenLLVM		*jit_genllvm;
	VexHelpers	*jit_vexhelpers;

	char		*saved_klee_cpustate;
	char		*saved_jit_cpustate;
	bool		exited;
};

}

#endif
