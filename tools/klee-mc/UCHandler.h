#ifndef UCHANDLER_H
#define UCHANDLER_H

#include "KleeHandlerVex.h"

namespace klee
{
class ExecutorVex;

class UCHandler : public KleeHandlerVex
{
public:
	UCHandler(const CmdArgs* cmdargs, Guest* g)
	: KleeHandlerVex(cmdargs, g) {}

	virtual ~UCHandler() {}
protected:
	virtual void processSuccessfulTest(
		const char* name, unsigned id, out_objs&);
	bool getStateSymObjs(
		const ExecutionState& state,
		out_objs& out);
private:

};
}

#endif
