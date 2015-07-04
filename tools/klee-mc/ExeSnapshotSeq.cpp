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

#define ESSTAG "[ExeSnapshotSeq] "

namespace
{
	llvm::cl::opt<bool> UsePrePost("use-prepost");
	llvm::cl::opt<bool> UseSSeqPre("use-sseq-pre");
	llvm::cl::opt<bool> UseSSeqPost("use-sseq-post");
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
{ ExecutorVex::handleXferSyscall(state, ki); }


void ExeSnapshotSeq::runLoop(void)
{
	std::cerr << ESSTAG "Loading Guests\n";
	loadGuestSequence();
	ExecutorVex::runLoop();
}

void ExeSnapshotSeq::addPrePostSeq(void)
{
	ExecutionState	*last_es(getCurrentState());

	for (unsigned i = 1; ; i++) {
		ExecutionState	*pre_es, *post_es;

		/* this is important so that the diffing is cheap */
		pre_es = addSequenceGuest(last_es, i, "-pre");
		last_es = pre_es;
		if (last_es == NULL) break;
		std::cerr << ESSTAG "PreGuest#" << i << '\n';
		es2esv(*pre_es).setSyscallCount(i);

		post_es = addSequenceGuest(last_es, i, "-post");
		last_es = post_es;
		if (last_es == NULL) break;
		std::cerr << ESSTAG "PostGuest#" << i << '\n';
		es2esv(*post_es).setSyscallCount(i);

	}

	assert (0 == 1 && "extra analysis???");
}

void ExeSnapshotSeq::addSeq(const char* suff)
{
	ExecutionState	*last_es(getCurrentState());
	unsigned	i;
	
	/* load all guests in sequence */
	for (i = 1; ; i++) {
		last_es = addSequenceGuest(last_es, i, suff);
		if (last_es == NULL) break;
		es2esv(*last_es).setSyscallCount(i);
	}

	std::cerr << ESSTAG "Added " << i-1 <<  " '" << suff << "' snapshots.\n";
}

void ExeSnapshotSeq::loadGuestSequence(void)
{
	if (UsePrePost) {
		/* add sequence of guests before *and* after syscall */
		addPrePostSeq();
	} else if (UseSSeqPre) {
		addSeq("-pre");
	} else if (UseSSeqPost) {
		addSeq("-post");
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
	std::unique_ptr<Guest>		new_gs;
	struct stat			st;
	unsigned			state_regctx_sz;
	ObjectState			*state_regctx_os;
	const char			*reg_data;
	char				s[512];
	KFunction			*kf;

	sprintf(s, "%s-%04d%s", base_guest_path.c_str(), i, suff);

	/* does snapshot directory exist? */
	if (stat(s, &st) == -1) {
		std::cerr << ESSTAG "missing " << s <<'\n';
		return NULL;
	}

	/* load with an offset so no conflicts with base guest */
	assert (getenv("VEXLLVM_BASE_BIAS") == NULL);
	setenv("VEXLLVM_BASE_BIAS", "0x8000000", 1);

	new_gs = Guest::load(s);

	/* XXX: this breaks kmc-replay because it will use the wrong
	 * base snapshot. crap! extend ktest format? */
	new_es = pureFork(*last_es);
	for (auto& m : new_gs->getMem()->getMaps()) {
		loadUpdatedMapping(new_es, new_gs.get(), m);
	}

	/* TODO track guest base in the state data, reflect in ktest writer */


	/* create register file */
	state_regctx_os = GETREGOBJ(*new_es);
	state_regctx_sz = new_gs->getCPUState()->getStateSize();
	reg_data = (const char*)new_gs->getCPUState()->getStateData();
	for (unsigned int i = 0; i < state_regctx_sz; i++)
		new_es->write8(state_regctx_os, i, reg_data[i]);

	kf = km_vex->getKFunction(
		km_vex->getFuncByAddr(new_gs->getCPUState()->getPC()));
	new_es->pc = kf->instructions;
	new_es->prevPC = new_es->pc;


	new_gs = nullptr;
	unsetenv("VEXLLVM_BASE_BIAS");

	return new_es;
}

unsigned ExeSnapshotSeq::loadUpdatedMapping(
	ExecutionState* new_es,
	Guest* new_gs,
	GuestMem::Mapping m)
{
	unsigned	pg_c = (m.length + 4095) / 4096;
	unsigned	reuse_c = 0;

	for (unsigned pgnum = 0; pgnum < pg_c; pgnum++) {
		bool		ok;
		ObjectPair	op;
		ObjectState	*os_w;
		guest_ptr	base(m.offset.o + pgnum*4096);
		const uint8_t	*pptr;

		ok = new_es->addressSpace.resolveOne(base, op);

		/* this page is not present; bind mapping */
		if (ok == false) {
			bindMappingPage(new_es, NULL, m, pgnum, new_gs);
		}

		assert (op_os(op)->isConcrete());

		pptr = (const uint8_t*)new_gs->getMem()->getHostPtr(base);

		/* no difference? */
		if (op_os(op)->cmpConcrete(pptr, 4096) == 0) {
			reuse_c++;
			assert (op_os(op)->getCopyDepth() == 0);
			continue;
		}

		/* found difference, write to new obj state */
		os_w = new_es->addressSpace.getWriteable(op);
		os_w->writeConcrete(pptr, 4096);
		os_w->resetCopyDepth();
	}

	return reuse_c;
}