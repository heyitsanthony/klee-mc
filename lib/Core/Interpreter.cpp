#include "klee/Interpreter.h"
#include "ExecutorBC.h"

using namespace klee;

Interpreter* Interpreter::create(InterpreterHandler *ih)
{
	return new ExecutorBC(ih);
}
