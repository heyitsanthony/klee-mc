#include "ChkExec.h"
#include "guest.h"
#include "guestcpustate.h"
#include "vexsb.h"
#include <stdlib.h>
#include "syscall/syscalls.h"
#include "ptimgchk.h"

#include <iostream>

#define DEFAULT_FIXUPS	1000

ChkExec::ChkExec(PTImgChk* gs, std::shared_ptr<VexXlate> vx)
: VexExecChk(gs, vx)
, print_exec(getenv("KMC_DUMP_EXE") != NULL)
, max_fixups(getenv("KMC_MAX_FIXUPS") != NULL
	? atoi(getenv("KMC_MAX_FIXUPS"))
	: DEFAULT_FIXUPS)
{}

guest_ptr ChkExec::doVexSB(VexSB* sb)
{
	if (print_exec) {
		fprintf(stderr, "[EXE] %p-%p %s\n",
			(void*)sb->getGuestAddr().o,
			(void*)sb->getEndAddr().o,
			gs->getName(sb->getGuestAddr()).c_str());
	}

	if (max_fixups) {
		unsigned	f_c;
		f_c = static_cast<PTImgChk*>(gs)->getNumFixups();
		if (f_c > max_fixups) {
			std::cerr
				<< "[ChkExec] Maximum fixups ("
				<< max_fixups
				<< ") exceeded. Fixups="
				<< f_c << '\n';
			exit(1);
		}
	}

	return VexExecChk::doVexSB(sb);
}

void ChkExec::doSysCall(VexSB* vsb)
{
	SyscallParams	sp(gs->getSyscallParams());
	uint64_t	ret;

	std::cerr << "[ChkExec] Dispatching local.\n";
	ret = sc->apply(sp);
	if (sc->isExit()) {
		std::cerr << "[ChkExec] Fixups: " 
			<< static_cast<PTImgChk*>(gs)->getNumFixups()
			<< '\n';

		setExit(ret);
		return;
	}
	gs->getCPUState()->setExitType(GE_RETURN);

	std::cerr << "[ChkExec] Dispatching remote.\n";
	sc_ptkt->apply(sp);

	std::cerr << "[ChkExec] Dispatched.\n";

	// no need to ignore the syscall because pushing the
	// guest registers to the shadow process changes the IP
	// to post-syscall

//	cross_check->ignoreSysCall();
//	assert (0 == 1 && "NEED TO ADVANCE PTRACE PROCESS");
//
//	static_cast<PTImgChk*>(gs)->printShadow(std::cerr);
}


void ChkExec::setSyscalls(std::unique_ptr<Syscalls> in_sc)
{
	/* make duplicate syscall handler for replaying into
	 * ptraced process */
	SyscallsKTest	*sc_kt = dynamic_cast<SyscallsKTest*>(in_sc.get());

	if (sc_kt == NULL)
		goto done;

	sc_ptkt = std::make_unique<SyscallsKTestPT>(sc_kt);
done:
	VexExecChk::setSyscalls(std::move(in_sc));
}
