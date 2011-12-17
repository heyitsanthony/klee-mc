#ifndef KLEE_EXEUC_H
#define KLEE_EXEUC_H

#include "ExecutorVex.h"

namespace klee
{
class ExeUC : public ExecutorVex
{
public:
	ExeUC(InterpreterHandler *ie, Guest* gs);
	virtual ~ExeUC();

	virtual void runImage(void);
	void setupUCEntry(
		ExecutionState* start_state,
		const char *xchk_fn);

protected:
	void runSym(const char* sym_name);

};
}

#endif
