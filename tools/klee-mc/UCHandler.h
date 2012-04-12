#ifndef UCHANDLER_H
#define UCHANDLER_H

#include "KleeHandler.h"

class CmdArgs;

namespace klee
{
class ExecutorVex;

class UCHandler : public KleeHandler
{
public:
	UCHandler(const CmdArgs* cmdargs, Guest* g)
	: KleeHandler(cmdargs, g) {}

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
