#include "ExeStateVex.h"

using namespace klee;

void ExeStateVex::recordRegisters(const void* reg, int sz)
{
	reg_log.push_back(std::vector<unsigned char>(
		(const unsigned char*)reg,
		(const unsigned char*)reg+sz));
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
