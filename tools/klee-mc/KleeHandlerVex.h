#ifndef KLEEHANDLERVEX_H
#define KLEEHANDLERVEX_H

#include "klee/Internal/ADT/KleeHandler.h"

class Guest;

namespace klee
{
class ExecutorVex;

class KleeHandlerVex : public KleeHandler
{
public:
	KleeHandlerVex(const CmdArgs* cmdargs, Guest *_gs);
	virtual ~KleeHandlerVex() {}

	Guest* getGuest(void) const { return gs; }
	virtual void setInterpreter(Interpreter *i);

	virtual unsigned processTestCase(
		const ExecutionState &state,
		const char *errorMessage,
		const char *errorSuffix);

protected:
	void printErrorMessage(
		const ExecutionState& state,
		const char* errorMessage,
		const char* errorSuffix,
		unsigned id);

private:
	void dumpLog(
		const ExecutionState& state, const char* name, unsigned id);

	bool validateTest(unsigned id);

	void printErrDump(
		const ExecutionState& state,
		std::ostream& os) const;

	Guest		*gs;
	ExecutorVex	*m_exevex;
};
}

#endif
