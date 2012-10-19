#include <iostream>
#include <assert.h>
#include "PartSeedCore.h"

using namespace klee;

PartSeedCore::PartSeedCore(Executor& _exe)
: exe(_exe)
{
	assert (0 == 1 && "STUB STUB STUB");
	// km->addFunctionPass(new PartSeedPass(exe));
}


void PartSeedCore::terminate(ExecutionState& state)
{
	assert (0 == 1 && "TERMINATING");
}
