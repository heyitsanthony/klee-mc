#ifndef EXESNAPSHOTSEQ_H
#define EXESNAPSHOTSEQ_H

#include "ExecutorVex.h"

/* XXX: this should maybe be a template/skin? */
namespace klee {

class ExeSnapshotSeq : public ExecutorVex
{
public:
	ExeSnapshotSeq(InterpreterHandler *ie);
	virtual ~ExeSnapshotSeq(void);

	// virtual void runImage(void);

	void setBaseName(const std::string& s);

protected:
	virtual void runLoop(void);
	virtual void handleXferSyscall(
		ExecutionState& state, KInstruction* ki);
private:
	ExecutionState* addSequenceGuest(
		ExecutionState* last_es, unsigned i);
	void loadUpdatedMapping(
		ExecutionState* new_es,
		Guest* new_gs,
		GuestMem::Mapping m);

	void loadPresentUpdatedMapping(
		ExecutionState* new_es,
		Guest* new_gs,
		GuestMem::Mapping m);


	unsigned	cur_seq;
	unsigned	sc_gap;
	unsigned	cur_sc;
	std::string	base_guest_path;
};
}

#endif
