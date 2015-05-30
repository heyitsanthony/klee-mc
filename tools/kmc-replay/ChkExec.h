#ifndef CHKEXEC_H
#define CHKEXEC_H

#include "SyscallsKTestPT.h"
#include "vexexecchk.h"

class ChkExec : public VexExecChk
{
public:
	ChkExec(PTImgChk* gs, std::shared_ptr<VexXlate> vx = nullptr);
	virtual ~ChkExec();
	virtual void setSyscalls(Syscalls* in_sc);
	virtual guest_ptr doVexSB(VexSB* sb);
protected:
	virtual void doSysCall(VexSB* vsb);

private:
	SyscallsKTestPT		*sc_ptkt;
	bool			print_exec;
	unsigned		max_fixups;
};

#endif
