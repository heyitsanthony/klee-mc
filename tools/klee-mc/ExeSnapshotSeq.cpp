#include "ExeSnapshotSeq.h"
#include "ExeStateVex.h"
#include "klee/Internal/Module/KFunction.h"
#include "KModuleVex.h"
#include "static/Sugar.h"

#include <llvm/Support/CommandLine.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iostream>

using namespace klee;

namespace
{
	llvm::cl::opt<bool> UsePrePost("use-prepost");
};


/* XXX: TODO TODO make sc_gap work */

ExeSnapshotSeq::ExeSnapshotSeq(InterpreterHandler *ie)
: ExecutorVex(ie)
, cur_seq(0)
, sc_gap(0)
{}

ExeSnapshotSeq::~ExeSnapshotSeq(void) {}


void ExeSnapshotSeq::setBaseName(const std::string& s)
{ base_guest_path = s; }

void ExeSnapshotSeq::handleXferSyscall(
	ExecutionState& state, KInstruction* ki)
{
	std::cerr << "hullo\n";
	ExecutorVex::handleXferSyscall(state, ki);
}


void ExeSnapshotSeq::runLoop(void)
{
	std::cerr << "this is where I should fork the initial state\n";

	ExecutorVex::runLoop();
}

void ExeSnapshotSeq::addPrePostSeq(void)
{
	ExecutionState	*last_es(getCurrentState());

	for (unsigned i = 1; ; i++) {
		last_es = addSequenceGuest(last_es, i, "-pre");
		if (last_es == NULL) break;
		std::cerr << "[ExeSnapshotSeq] PreGuest#" << i << '\n';

		last_es = addSequenceGuest(last_es, i, "-post");
		if (last_es == NULL) break;
		std::cerr << "[ExeSnapshotSeq] PostGuest#" << i << '\n';

	}


	assert (0 == 1 && "extra analysis???");
}

void ExeSnapshotSeq::addPreSeq(void)
{
	ExecutionState	*last_es(getCurrentState());
	/* load all guests in sequence */
	for (unsigned i = 1; ; i++) {
		last_es = addSequenceGuest(last_es, i);
		if (last_es == NULL) break;
		std::cerr << "[ExeSnapshotSeq] Loaded Guest #" << i << '\n';
	}
}

void ExeSnapshotSeq::loadGuestSequence(void)
{
	if (UsePrePost) {
		/* add sequence of guests before *and* after syscall */
		addPrePostSeq();
	} else {
		/* add sequence of guests all before syscall */
		addPreSeq();
	}
}

ExecutionState* ExeSnapshotSeq::addSequenceGuest(
	ExecutionState* last_es, unsigned i,
	const char *suff)
{
	ExecutionState			*new_es;
	Guest				*new_gs;
	std::list<GuestMem::Mapping>	maps;
	struct stat			st;
	unsigned			state_regctx_sz;
	ObjectState			*state_regctx_os;
	const char			*reg_data;
	char				s[512];
	KFunction			*kf;


	sprintf(s, "%s-%04d%s", base_guest_path.c_str(), i, suff);

	/* does snapshot directory exist? */
	if (stat(s, &st) == -1) return NULL;

	/* load with an offset so no conflicts with base guest */
	assert (getenv("VEXLLVM_BASE_BIAS") == NULL);
	setenv("VEXLLVM_BASE_BIAS", "0x8000000", 1);

	new_gs = Guest::load(s);
	std::cerr << "loaded guest #" << i << '\n';

	/* XXX: this breaks kmc-replay because it will use the wrong
	 * base snapshot. crap! */
	new_es = pureFork(*last_es);
	maps = new_gs->getMem()->getMaps();
	foreach (it, maps.begin(), maps.end())
		loadUpdatedMapping(new_es, new_gs, *it);

	state_regctx_os = GETREGOBJ(*new_es);
	state_regctx_sz = new_gs->getCPUState()->getStateSize();
	reg_data = (const char*)new_gs->getCPUState()->getStateData();
	for (unsigned int i = 0; i < state_regctx_sz; i++)
		new_es->write8(state_regctx_os, i, reg_data[i]);

	kf = km_vex->getKFunction(
		km_vex->getFuncByAddr(new_gs->getCPUState()->getPC()));
	new_es->pc = kf->instructions;
	new_es->prevPC = new_es->pc;


	delete new_gs;
	unsetenv("VEXLLVM_BASE_BIAS");

	return new_es;
}

void ExeSnapshotSeq::loadUpdatedMapping(
	ExecutionState* new_es,
	Guest* new_gs,
	GuestMem::Mapping m)
{
	unsigned pg_c = m.length / 4096;

	for (unsigned pgnum = 0; pgnum < pg_c; pgnum++) {
		bool		ok;
		ObjectPair	op;
		ObjectState	*os_w;
		const uint8_t	*pptr;

		ok = new_es->addressSpace.resolveOne(
			m.offset.o + pgnum*4096,
			op);

		/* this page is not present; bind mapping */
		if (ok == false) {
			bindMappingPage(new_es, NULL, m, pgnum, new_gs);
		}

		assert (op_os(op)->isConcrete());

		pptr = (const uint8_t*)
			new_gs->getMem()->getHostPtr(m.offset+(4096*pgnum));

		/* no difference? */
		if (op_os(op)->cmpConcrete(pptr, 4096) == 0)
			continue;

		/* found difference, write to new obj state */
		os_w = new_es->addressSpace.getWriteable(op);
		os_w->writeConcrete(pptr, 4096);
	}
}