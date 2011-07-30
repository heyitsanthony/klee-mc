#include "ReplayExec.h"
#include "SyscallsKTest.h"
#include "guest.h"
#include "guestcpustate.h"
#include "vexsb.h"
#include <stdlib.h>
#include <string.h>
extern "C"
{
#include <valgrind/libvex_guest_amd64.h>
}

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
	if (skt->isExit()) setExit(ret);
}

guest_ptr ReplayExec::doVexSB(VexSB* sb)
{
	guest_ptr	next_pc;
	uint8_t		*reg_mismatch;
	
	next_pc = VexExec::doVexSB(sb);

	if ((reg_mismatch = verifyWithRegLog()) == NULL) {
		return next_pc;
	}

	std::cerr << "============doVexSB Mismatch!============\n";
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



	delete [] reg_mismatch;
	exit(1);

	return next_pc;
}

ReplayExec::~ReplayExec() {}

uint8_t* ReplayExec::verifyWithRegLog(void)
{
	return NULL;
#if 0
	uint8_t		*mask_buf, *reg_buf;
	const uint8_t	*guest_buf;
	unsigned int	reg_sz;
	reg_buf = feedRegLog();
	if (reg_buf == NULL) return NULL;

	mask_buf = feedRegLog();
	if (mask_buf == NULL) {
		delete [] reg_buf;
		return NULL;
	}

	reg_sz = gs->getCPUState()->getStateSize();
	guest_buf = (const uint8_t*)gs->getCPUState()->getStateData();

	for (unsigned int i = 0; i < reg_sz; i++) {
		if (!mask_buf[i]) continue;
		if (reg_buf[i] != guest_buf[i]) {
			fprintf(stderr, "====MASK:\n");
			dumpRegBuf(mask_buf);
			delete [] mask_buf;
			return reg_buf;
		}
		
	}

	fprintf(stderr, "REGLOGOK\n");
	delete [] mask_buf;
	delete [] reg_buf;
	return NULL;
#endif
}

uint8_t* ReplayExec::feedRegLog(void)
{
#if 0
	uint8_t		*ret;
	unsigned int	reg_sz;
	ssize_t		br;
	
	if (f_reglog == NULL) return NULL;

	reg_sz = gs->getCPUState()->getStateSize();
	ret = new uint8_t[reg_sz];
	br = fread(ret, reg_sz, 1, f_reglog);
	if (br != 1) {
		delete [] ret;
		return NULL;
	}

	return ret;
#endif
	return NULL;
}
