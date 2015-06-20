#ifndef CHKEXEC_H
#define CHKEXEC_H

#include "SyscallsKTestPT.h"
#include "vexexecchk.h"

class ChkExec : public VexExecChk
{
public:
	ChkExec(PTImgChk* gs, std::shared_ptr<VexXlate> vx = nullptr);
	void setSyscalls(std::unique_ptr<Syscalls> in_sc) override;
	guest_ptr doVexSB(VexSB* sb) override;

protected:
	void doSysCall(VexSB* vsb) override;

private:
	std::unique_ptr<SyscallsKTestPT> sc_ptkt;
	bool			print_exec;
	unsigned		max_fixups;
};

#endif
