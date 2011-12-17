#include <assert.h>
#include "symbols.h"
#include "guestcpustate.h"

#include "ExeStateVex.h"
#include "ExeUC.h"

using namespace klee;

extern bool SymRegs;

ExeUC::ExeUC(InterpreterHandler *ie, Guest* gs)
: ExecutorVex(ie, gs)
{
	if (!SymRegs) {
		fprintf(stderr,
			"[EXEUC] Forcing Symregs. Use -symregs next time?\n");
		SymRegs = true;
	}
}

ExeUC::~ExeUC() {}

void ExeUC::runImage(void)
{
	const char	*xchk_fn[] = {
		"memcpy",
		"memset",
		"printf",
		"gettimeofday",
		NULL};
	
	for (unsigned i = 0; xchk_fn[i]; i++) {
		runSym(xchk_fn[i]);
	}

	fprintf(stderr, "DONE FOR THE DAY\n");
}

void ExeUC::runSym(const char* xchk_fn)
{
	ExecutionState	*start_state;
	const Symbols	*syms;
	const Symbol	*sym;


	fprintf(stderr, "[EXEUC] FINDING SYM: %s\n", xchk_fn);
	syms = gs->getSymbols();
	sym = syms->findSym(xchk_fn);

	assert (sym != NULL && "Couldn't find sym");

	start_state = setupInitialStateEntry(sym->getBaseAddr());
	if (start_state == NULL)
		return;

	setupUCEntry(start_state, xchk_fn);

	fprintf(stderr, "[EXEUC] RUNNING: %s\n", xchk_fn);

	run(*start_state);

	cleanupImage();
	fprintf(stderr, "[EXEUC] OK.\n");
}

void ExeUC::setupUCEntry(
	ExecutionState* start_state,
	const char *xchk_fn)
{

	/* mark register state as symbolic */
	/* 
	const std::string& getName() const { return name; }
	symaddr_t getBaseAddr() const { return base_addr; }
	symaddr_t getEndAddr() const { return base_addr + length; }
	unsigned int getLength() const { return length; }
	*/


	ObjectState	*reg_os;
	unsigned	ptr_bytes = (gs->getMem()->is32Bit()) ? 4 : 8;
	uint64_t	off;

	reg_os = GETREGOBJ(*start_state);
	const char*  state_data = (const char*)gs->getCPUState()->getStateData();
	off = gs->getCPUState()->getStackRegOff();
	for (unsigned i=0; i < ptr_bytes; i++) {
		start_state->write8(reg_os, off+i, state_data[off+i]);
	}

	//assert (0 == 1 && "STUBBOB");
	fprintf(stderr, "[EXEUC] OFF THE DEEP END\n");
}

