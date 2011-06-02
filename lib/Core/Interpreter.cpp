#include "klee/Interpreter.h"
#include "ExecutorBC.h"

using namespace klee;

Interpreter* Interpreter::create(
	const InterpreterOptions &opts,
	InterpreterHandler *ih)
{
	return new ExecutorBC(opts, ih);
}
