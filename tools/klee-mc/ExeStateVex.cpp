#include "ExeStateVex.h"
#include "klee/breadcrumb.h"

using namespace klee;

void ExeStateVex::recordRegisters(const void* reg, int sz)
{
/* XXX */
}


ExeStateVex::ExeStateVex(const ExeStateVex& src)
: ExecutionState(src)
, syscall_c(0)
{
	const ExeStateVex	*esv;
	
	esv = dynamic_cast<const ExeStateVex*>(&src);
	assert (esv != NULL && "Copying non-ESV?");

	bc_log = esv->bc_log;
	reg_mo = esv->reg_mo;
}

void ExeStateVex::recordBreadcrumb(const struct breadcrumb* bc)
{
	bc_log.push_back(std::vector<unsigned char>(
		(const unsigned char*)bc,
		((const unsigned char*)bc) + bc->bc_sz));
}
