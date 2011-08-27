#include "ReplayExec.h"
#include "SyscallsKTest.h"
#include "guest.h"
#include "guestcpustate.h"
#include "vexsb.h"
#include <stdlib.h>
#include <string.h>
#include "klee/Internal/ADT/Crumbs.h"
#include "klee/breadcrumb.h"

using namespace klee;

extern "C"
{
#include <valgrind/libvex_guest_amd64.h>
}

/* XXX HACK HACK HACK. See below */
#define is_syspage_addr(x)	\
	(((uintptr_t)x)>=0xffffffffff600000 && ((uintptr_t)x)<0xffffffffff601000)

ReplayExec::ReplayExec(Guest* gs, VexXlate* vx)
: VexExec(gs, vx)
, skt(NULL)
, has_reglog(false)
, ign_reglog(getenv("KMC_REPLAY_IGNLOG") != NULL)
, crumbs(NULL)
{ }

void ReplayExec::setSyscallsKTest(SyscallsKTest* in_skt)
{
	delete sc;
	skt = in_skt;
	sc = skt;
}

void ReplayExec::dumpRegBuf(const uint8_t* buf)
{
	for (unsigned int i = 0; i < 633; i++) {
		if ((i % 16) == 0) fprintf(stderr, "\n%03x: ", i);
		fprintf(stderr, "%02x ", buf[i]);
	}
	fprintf(stderr, "\n");
}

void ReplayExec::doSysCall(VexSB* sb)
{
	SyscallParams	sp(gs->getSyscallParams());
	uint64_t	ret;

	ret = skt->apply(sp);
	if (skt->isExit()) {
		setExit(ret);
		return;
	}

	fprintf(stderr, "VERIFY AFTER SYSCALL\n");
	gs->getCPUState()->setExitType(GE_RETURN);
	verifyOrPanic();
}

void ReplayExec::verifyOrPanic(void)
{
	uint8_t		*reg_mismatch;
	
	if ((reg_mismatch = verifyWithRegLog()) == NULL)
		return;

	std::cerr << "============doVexSB Mismatch!============\n";
	std::cerr << "Crumbs read: " << crumbs->getNumProcessed() << std::endl;
	std::cerr << "VEXEXEC (now running): \n";
	gs->getCPUState()->print(std::cerr);
	std::cerr << "\nKLEE-MC (previously ran): \n";
	gs->getCPUState()->print(std::cerr, reg_mismatch);
	std::cerr << "\n";

	const uint8_t	*vex_regs;
	vex_regs = (const uint8_t*)gs->getCPUState()->getStateData();

	fprintf(stderr, "VEX: ----------------\n");
	dumpRegBuf(vex_regs);

	fprintf(stderr, "KLEE: ----------------\n");
	for (unsigned int i = 0; i < 633; i++) {
		if ((i % 16) == 0) fprintf(stderr, "\n%03x: ", i);
		fprintf(stderr, "%02x%c", reg_mismatch[i],
			(reg_mismatch[i] != vex_regs[i]) ? '*' : ' ');
	}
	fprintf(stderr, "\n");
#if 0
	fprintf(stderr,
		"*(RDI): %p\n",
		*((uint64_t*)(((VexGuestAMD64State*)vex_regs)->guest_RDI)));
#endif
	delete [] reg_mismatch;
	exit(-1);
	assert (0 == 1);
}

guest_ptr ReplayExec::doVexSB(VexSB* sb)
{
	guest_ptr	next_pc;

	next_pc = VexExec::doVexSB(sb);
	verifyOrPanic();

	return next_pc;
}

ReplayExec::~ReplayExec() {}

void ReplayExec::setCrumbs(Crumbs* in_c)
{
	crumbs = in_c;
	has_reglog = crumbs->hasType(BC_TYPE_VEXREG);
	if (!has_reglog) {
		fprintf(stderr, "[kmc-info]: No reglog. Unverified!\n");
	}
}

uint8_t* ReplayExec::verifyWithRegLog(void)
{
	struct breadcrumb	*bc;
	uint8_t			*sym_reg, *guest_reg, *sym_mask, *buf;
	unsigned int		reg_sz;
	guest_ptr		next_addr;
	bool			is_next_guest_vsys, is_next_sym_vsys;
	uint8_t			*reg_ctx = NULL;	/* bad ctx */

	if (!has_reglog) return NULL;

	bc = crumbs->peek();
	if (bc == NULL) {
		has_reglog = false;
		fprintf(stderr,
			"Ran out of register log. Mismatch? Keep going.\n");
		return NULL;
	}
	assert (bc_is_type(bc, BC_TYPE_VEXREG));
	if (ign_reglog) goto done;

	reg_sz = gs->getCPUState()->getStateSize();
	assert (bc->bc_sz == reg_sz*2 + sizeof(struct breadcrumb));

	guest_reg = (uint8_t*)gs->getCPUState()->getStateData();
	buf = reinterpret_cast<uint8_t*>(bc) + sizeof(struct breadcrumb);
	sym_reg = buf;
	sym_mask = buf + reg_sz;

	/* XXX HACK HACK HACK. There's something in the syspage that changes
	 * from process to process. Since we can't write to the syspage with
	 * the guest snapshot's old syspage, all we can do is 'bless' the region
	 * and temporarily grant blind amnesty. */
	next_addr = gs->getCPUState()->getPC();
	is_next_guest_vsys = is_syspage_addr(next_addr);
	is_next_sym_vsys = is_syspage_addr(
		guest_ptr(((VexGuestAMD64State*)sym_reg)->guest_RIP));

	if (is_next_guest_vsys) {
		/* Don't skip() over the klee state.
		 * First, run all guest code until exiting vsys, *then* skip over
		 * the register log.*/
		Crumbs::freeCrumb(bc);
		fprintf(stderr,
			"Ignoring syscall page. #%d\n",
			crumbs->getNumProcessed());
		ignored_last = true;
		skipped_vsys = true;
		return NULL;
	}

	/* Not on a vsyspage in guest any more.
	 * OK to skip sym log until we hit a non-vsyspage in symbolic. */
	crumbs->skip();
	if (is_next_sym_vsys) {
		fprintf(stderr,
			"Skipping crumb #%d\n",
			crumbs->getNumProcessed());
		Crumbs::freeCrumb(bc);
		skipped_vsys = true;
		return verifyWithRegLog();
	}


	fprintf(stderr, "-------CHKLOG %d. LastAddr=%p------\n",
		crumbs->getNumProcessed(),
		(void*)next_addr.o);
	for (unsigned int i = 0; i < reg_sz; i++) {
		if (!sym_mask[i]) continue;
		if (sym_reg[i] == guest_reg[i]) continue;

		/* mismatch */

		/* HACK */
		if (ignored_last) {
			fprintf(stderr, "FAKING [0x%x](0x%x) <- 0x%x\n",
				i, guest_reg[i], sym_reg[i]);
			guest_reg[i] = sym_reg[i];
			continue;
		}

		fprintf(stderr, "====MASK:\n");
		dumpRegBuf(sym_mask);

		reg_ctx = new uint8_t[reg_sz];
		memcpy(reg_ctx, sym_reg, reg_sz);
		goto done;
	}

	reg_ctx = NULL;

done:
	Crumbs::freeCrumb(bc);
	ignored_last = false;
	return reg_ctx;
}
