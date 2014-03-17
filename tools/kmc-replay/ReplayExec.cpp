#include "ReplayExec.h"
#include "SyscallsKTest.h"
#include "guest.h"
#include "guestcpustate.h"
#include "vexsb.h"
#include <stdlib.h>
#include <string.h>
#include "klee/Internal/ADT/Crumbs.h"
#include "klee/breadcrumb.h"
#include "static/Sugar.h"

using namespace klee;

extern "C"
{
#include <valgrind/libvex_guest_amd64.h>
#include <valgrind/libvex_guest_x86.h>
}

/* XXX HACK HACK HACK. See below */
#define is_syspage_addr(x)	\
	(is_vdso_patched == false && \
	(((uintptr_t)x)>=0xffffffffff600000 && ((uintptr_t)x)<0xffffffffff601000))

ReplayExec::ReplayExec(Guest* gs, VexXlate* vx)
: VexExec(gs, vx)
, has_reglog(false)
, ign_reglog(getenv("KMC_REPLAY_IGNLOG") != NULL)
, crumbs(NULL)
, ignored_last(false)
, print_exec(getenv("KMC_DUMP_EXE") != NULL)
, is_vdso_patched(gs->isPatchedVDSO())
, chklog_c(0)
{ }

unsigned ReplayExec::getCPUSize(void)
{ return gs->getCPUState()->getStateSize(); }


static void dumpBuf(const uint8_t* buf, unsigned len)
{
	for (unsigned int i = 0; i < len; i++) {
		if ((i % 16) == 0) fprintf(stderr, "\n%03x: ", i);
		fprintf(stderr, "%02x ", buf[i]);
	}
	fprintf(stderr, "\n");
}

void ReplayExec::dumpRegBuf(const uint8_t* buf)
{ dumpBuf(buf, getCPUSize()); }

void ReplayExec::doSysCall(VexSB* sb)
{
	SyscallParams	sp(gs->getSyscallParams());
	uint64_t	ret;

	ret = sc->apply(sp);
	if (sc->isExit()) {
		setExit(ret);
		return;
	}

	gs->getCPUState()->setExitType(GE_RETURN);

	if (has_reglog)
		fprintf(stderr, "VERIFY AFTER SYSCALL\n");
	verifyOrPanic();
}

void ReplayExec::verifyOrPanic(VexSB* last_dispatched)
{
	ReplayMismatch	*m;

	if ((m = verifyWithLog()) == NULL)
		return;

	/* TODO: one option here is to do a fixup and proceed as if
	 * it were symex to make forward progress.
	 * note, it doesn't make sense to use current state
	 * because then the syscall log won't match up) */
	std::cerr
		<< "[ReplayExec] Crumbs read: "
		<< crumbs->getNumProcessed() << std::endl;

	if (last_dispatched != NULL) {
		std::cerr
			<< "[ReplayExec] Begin Last Dispatched Block. Base="
			<< (void*)last_dispatched->getGuestAddr().o
			<< '\n';
		last_dispatched->print(std::cerr);
		std::cerr << "[ReplayExec] End of Last Dispatched Block.\n";
	}

	m->print(std::cerr);
	exit(-1);
}

void StackReplayMismatch::print(std::ostream& os)
{
	const uint8_t	*guest_stk;

	guest_stk = (uint8_t*)gs->getCPUState()->getStackPtr().o;

	os << "============doVexSB Stack Mismatch!============\n";
	os << "Stack Address = " << (void*)guest_stk << '\n';
	os << "VEXEXEC (now running): \n";
	for (unsigned int i = 0; i < len; i++) {
		if ((i % 16) == 0) fprintf(stderr, "\n%03x: ", i);
		fprintf(stderr, "%02x ", guest_stk[i]);
	}

	os << "\nKLEE-MC (previously ran): \n";
	for (unsigned int i = 0; i < len; i++) {
		if ((i % 16) == 0) fprintf(stderr, "\n%03x: ", i);
		fprintf(stderr, "%02x%c", sym_stk[i],
			(sym_stk[i] != guest_stk[i]) ? '*' : ' ');
	}
	std::cerr << '\n';
}

void MemReplayMismatch::print(std::ostream& os)
{
	const uint8_t	*guest_obj = (uint8_t*)base;

	os << "============Memory Mismatch!============\n";
	os << "Memory Base Address = " << (void*)guest_obj << '\n';
	os << "VEXEXEC (now running): \n";
	for (unsigned int i = 0; i < len; i++) {
		if ((i % 16) == 0) fprintf(stderr, "\n%03x: ", i);
		fprintf(stderr, "%02x ", guest_obj[i]);
	}

	os << "\nKLEE-MC (previously ran): \n";
	for (unsigned int i = 0; i < len; i++) {
		if ((i % 16) == 0) fprintf(stderr, "\n%03x: ", i);
		fprintf(stderr, "%02x%c", sym_obj[i],
			(sym_obj[i] != guest_obj[i]) ? '*' : ' ');
	}
	std::cerr << '\n';
}

void RegReplayMismatch::print(std::ostream& os)
{
	const uint8_t	*vex_regs;
	unsigned	cpu_len = gs->getCPUState()->getStateSize();

	os << "============doVexSB Mismatch!============\n";
	os << "VEXEXEC (now running): \n";
	gs->getCPUState()->print(os);
	os << "\nKLEE-MC (previously ran): \n";
	gs->getCPUState()->print(std::cerr, reg_mismatch);
	std::cerr << "\n";

	vex_regs = (const uint8_t*)gs->getCPUState()->getStateData();

	os << "VEX: ----------------\n";
	dumpBuf(vex_regs, cpu_len);

	os << "KLEE: ----------------\n";
	for (unsigned int i = 0; i < cpu_len; i++) {
		if ((i % 16) == 0) fprintf(stderr, "\n%03x: ", i);
		fprintf(stderr, "%02x%c", reg_mismatch[i],
			(reg_mismatch[i] != vex_regs[i]) ? '*' : ' ');
	}
	os << '\n';
}

void ReplayExec::doTrap(VexSB* sb)
{
	fprintf(stderr, "[EXE] Hit Trap. Exiting\n");
	setExit(0x0c0ffee);
}

guest_ptr ReplayExec::doVexSB(VexSB* sb)
{
	guest_ptr	next_pc;

	if (print_exec) {
		fprintf(stderr, "[EXE] %p-%p %s\n",
			(void*)sb->getGuestAddr().o,
			(void*)sb->getEndAddr().o,
			gs->getName(sb->getGuestAddr()).c_str());
	}

	next_pc = VexExec::doVexSB(sb);
	verifyOrPanic(sb);

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

ReplayMismatch*	ReplayExec::doChks(struct breadcrumb* &bc)
{
	uint8_t				*buf;
	std::vector<ReplayMismatch*>	m;
	ReplayMismatch			*m0;
	regchk_t			regchk;
	bool				has_stacklog, has_memlog;

	regchk.reg_sz = getCPUSize();
	assert (bc->bc_sz == regchk.reg_sz*2 + sizeof(struct breadcrumb));

	regchk.guest_reg = (uint8_t*)gs->getCPUState()->getStateData();
	buf = reinterpret_cast<uint8_t*>(bc) + sizeof(struct breadcrumb);
	regchk.sym_reg = buf;
	regchk.sym_mask = buf + regchk.reg_sz;


	if ((m0 = regChk(regchk))) m.push_back(m0);

	Crumbs::freeCrumb(bc);
	bc = crumbs->peek();

	has_stacklog = bc != NULL && bc_is_type(bc, BC_TYPE_STACKLOG);
	if (has_stacklog) {
		regchk.reg_sz = (bc->bc_sz-sizeof(struct breadcrumb)) / 2;
		regchk.guest_reg = (uint8_t*)gs->getCPUState()->getStackPtr().o;
		buf = reinterpret_cast<uint8_t*>(bc)+sizeof(struct breadcrumb);
		regchk.sym_reg = buf;
		regchk.sym_mask = buf + regchk.reg_sz;
		if (ignored_last == false) {
			if ((m0 = stackChk(regchk))) m.push_back(m0);
		}
	}

	Crumbs::freeCrumb(bc);
	bc = crumbs->peek();

	has_memlog = bc != NULL && bc_is_type(bc, BC_TYPE_MEMLOG);
	if (has_memlog) {
		uint64_t	base;
		crumbs->skip();
		regchk.reg_sz = (bc->bc_sz-(sizeof(struct breadcrumb)+8)) / 2;
		base = *((uint64_t*)(bc + 1));
		regchk.guest_reg = (uint8_t*)base;
		buf = reinterpret_cast<uint8_t*>(bc)+sizeof(struct breadcrumb)+8;
		regchk.sym_reg = buf;
		regchk.sym_mask = buf + regchk.reg_sz;
		if (ignored_last == false)
			if ((m0 = memChk(regchk))) m.push_back(m0);
	}


	return MultiReplayMismatch::create(m);
}

ReplayMismatch* ReplayExec::verifyWithLog(void)
{
	struct breadcrumb	*bc;
	guest_ptr		next_addr;
	bool			is_next_guest_vsys, is_next_sym_vsys;
	ReplayMismatch		*m = NULL;

	if (!has_reglog) return NULL;

	bc = crumbs->peek();
	if (bc == NULL) {
		has_reglog = false;
		fprintf(stderr,
			"Ran out of register log. Mismatch? Keep going.\n");
		return NULL;
	}

	assert (bc_is_type(bc, BC_TYPE_VEXREG));

	/* XXX HACK HACK HACK. There's something in the syspage that changes
	 * from process to process. Since we can't write to the syspage with
	 * the guest snapshot's old syspage, all we can do is 'bless' the region
	 * and temporarily grant blind amnesty. */
	next_addr = gs->getCPUState()->getPC();
	is_next_guest_vsys = is_syspage_addr(next_addr);
	is_next_sym_vsys = is_syspage_addr(
		guest_ptr(((VexGuestAMD64State*)((char*)(bc+1)))->guest_RIP));

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
		return verifyWithLog();
	}


	if (!ign_reglog)
		fprintf(stderr, "-------CHKLOG %d. LastAddr=%p (%s)------\n",
			++chklog_c,
			(void*)next_addr.o,
			gs->getName(next_addr).c_str());

	m = doChks(bc);

	Crumbs::freeCrumb(bc);
	ignored_last = false;
	return m;
}

RegReplayMismatch* ReplayExec::regChk(const struct regchk_t& rc)
{
	if (ign_reglog) return NULL;

	if (getGuest()->getArch() == Arch::I386) {
		VexGuestX86State	*v = (VexGuestX86State*)rc.sym_mask;
		/* special hack to ignore ldt and gdt pointers.
		 * kind of stupid, but I'm on deadline for CCS */
		v->guest_LDT = 0;
		v->guest_GDT = 0;
	}

	for (unsigned int i = 0; i < rc.reg_sz; i++) {
		uint8_t*	new_reg_ctx;

		if (!rc.sym_mask[i]) continue;
		if (rc.sym_reg[i] == rc.guest_reg[i]) continue;

		/* mismatch */

		/* HACK */
		if (ignored_last) {
			fprintf(stderr, "FAKING [0x%x](0x%x) <- 0x%x\n",
				i, rc.guest_reg[i], rc.sym_reg[i]);
			rc.guest_reg[i] = rc.sym_reg[i];
			continue;
		}

		fprintf(stderr, ">>>>>> Mismatch on idx=0x%x\n", i);
		fprintf(stderr, "====MASK:\n");
		dumpRegBuf(rc.sym_mask);

		new_reg_ctx = new uint8_t[rc.reg_sz];
		memcpy(new_reg_ctx, rc.sym_reg, rc.reg_sz);
		return new RegReplayMismatch(gs, new_reg_ctx);
	}

	return NULL;
}

MemReplayMismatch* ReplayExec::memChk(const struct regchk_t& rc)
{
	for (unsigned int i = 0; i < rc.reg_sz; i++) {
		uint8_t*	new_reg_ctx;

		if (!rc.sym_mask[i]) continue;
		if (rc.sym_reg[i] == rc.guest_reg[i]) continue;

		/* mismatch */
		fprintf(stderr, ">>>>>> Mem Mismatch on idx=0x%x\n", i);
		fprintf(stderr, "====MASK:\n");
		dumpBuf(rc.sym_mask, rc.reg_sz);

		new_reg_ctx = new uint8_t[rc.reg_sz];
		memcpy(new_reg_ctx, rc.sym_reg, rc.reg_sz);
		return new MemReplayMismatch(
			gs, rc.guest_reg, new_reg_ctx, rc.reg_sz);
	}

	return NULL;
}

StackReplayMismatch* ReplayExec::stackChk(const struct regchk_t& rc)
{
	for (unsigned int i = 0; i < rc.reg_sz; i++) {
		uint8_t*	new_reg_ctx;

		if (!rc.sym_mask[i]) continue;
		if (rc.sym_reg[i] == rc.guest_reg[i]) continue;

		/* mismatch */
		fprintf(stderr, ">>>>>> Stack Mismatch on idx=0x%x\n", i);
		fprintf(stderr, "====MASK:\n");
		dumpBuf(rc.sym_mask, rc.reg_sz);

		new_reg_ctx = new uint8_t[rc.reg_sz];
		memcpy(new_reg_ctx, rc.sym_reg, rc.reg_sz);
		return new StackReplayMismatch(gs, new_reg_ctx, rc.reg_sz);
	}

	return NULL;
}

MultiReplayMismatch::~MultiReplayMismatch(void)
{ foreach (it, m.begin(), m.end()) delete (*it); }

void MultiReplayMismatch::print(std::ostream& os)
{ foreach (it, m.begin(), m.end()) (*it)->print(os); }


ReplayMismatch* MultiReplayMismatch::create(
	const std::vector<ReplayMismatch*>& v)
{
	if (v.empty()) return NULL;
	if (v.size() == 1) return v[0];
	return new MultiReplayMismatch(v);
}
