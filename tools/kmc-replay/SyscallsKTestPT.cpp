#include "klee/Internal/ADT/KTestStream.h"
#include "klee/Internal/ADT/Crumbs.h"
#include "FileReconstructor.h"
#include "SyscallsKTestPT.h"
#include "guestptmem.h"
#include "guestcpustate.h"
#include "ptimgchk.h"

SyscallsKTestPT::SyscallsKTestPT(const SyscallsKTest* sc_kt)
: SyscallsKTest(
	sc_kt->guest,
	sc_kt->kts->copy(),
	sc_kt->crumbs->copy())
{
	PTImgChk	*pt_g;

	/* never use file recons-- syscallsktest should do it */
	if (file_recons) delete file_recons;
	file_recons = NULL;

	pt_g = dynamic_cast<PTImgChk*>(guest);
	assert (pt_g != NULL);

	pt_mem = new GuestPTMem(pt_g, pt_g->getPID());
}

SyscallsKTestPT::~SyscallsKTestPT(void)
{
	delete pt_mem;
	delete crumbs;
}

bool SyscallsKTestPT::copyInRegMemObj(void)
{
	char			*partial_reg_buf;
	unsigned int		reg_sz;
	bool			ok;

	reg_sz = guest->getCPUState()->getStateSize();
	partial_reg_buf = kts->feedObjData(reg_sz);
	ok = partial_reg_buf != NULL;
	if (ok) delete partial_reg_buf;

	/* guest registers should already have this copied in... */
	static_cast<PTImgChk*>(guest)->pushRegisters();
	ret_set |= ok;
	return ok;
}

void SyscallsKTestPT::setRet(uint64_t r)
{
	if (!ret_set)
		static_cast<PTImgChk*>(guest)->pushRegisters();
	ret_set = true;
}


uint64_t SyscallsKTestPT::apply(SyscallParams& sp)
{
	uint64_t	ret;
	GuestMem	*old_mem;

	ret_set = false;

	old_mem = guest->getMem();
	guest->setMem(pt_mem);
	ret = SyscallsKTest::apply(sp);
	guest->setMem(old_mem);

	return ret;
}
