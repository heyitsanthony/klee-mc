#include "ExeStateVex.h"

using namespace klee;

void ExeStateVex::recordRegisters(const void* reg, int sz)
{
	reg_log.push_back(std::vector<unsigned char>(
		(const unsigned char*)reg,
		(const unsigned char*)reg+sz));
}


void ExeStateVex::recordSyscall(uint64_t sysnr, uint64_t aux, uint64_t flags)
{
	uint64_t	buf[3];
	buf[0] = sysnr; buf[1] = aux; buf[2] = flags;
	sc_log.push_back(std::vector<unsigned char>(
		(const unsigned char*)buf,
		(const unsigned char*)(buf+3)));
}

ExeStateVex::ExeStateVex(const ExeStateVex& src)
: ExecutionState(src)
{
	const ExeStateVex	*esv;
	
	esv = dynamic_cast<const ExeStateVex*>(&src);
	assert (esv != NULL && "Copying non-ESV?");

	reg_log = esv->reg_log;
	sc_log = esv->sc_log;
	reg_mo = esv->reg_mo;
}
