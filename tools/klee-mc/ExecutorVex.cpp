#include "gueststate.h"
#include "genllvm.h"
#include "vexhelpers.h"
#include "vexxlate.h"

#include "ExecutorVex.h"

using namespace klee;

ExecutorVex::ExecutorVex(
	const InterpreterOptions &opts,
	InterpreterHandler *ie,
	GuestState	*in_gs)
: Executor(opts, ie),
  kmodule(0),
  gs(in_gs)
{
	assert (gs);

	if (!theGenLLVM) theGenLLVM = new GenLLVM(in_gs);
	if (!theVexHelpers) theVexHelpers = new VexHelpers();

	xlate = new VexXlate();
}

ExecutorVex::~ExecutorVex(void)
{
	if (kmodule) delete kmodule;
}

void ExecutorVex::runImage(void)
{
	assert (0 == 1 && "URKKKKK");
}
