#include "klee/ExecutionState.h"

#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KFunction.h"

#include "klee/Expr.h"
#include "static/Sugar.h"

#include <cassert>
#include <map>
#include <set>

using namespace klee;

StackFrame::StackFrame(KInstIterator _caller, KFunction *_kf)
: call(_kf->callcount++)
, caller(_caller)
, kf(_kf)
, callPathNode(0)
, minDistToUncoveredOnReturn(0)
, varargs(0)
{
	locals = new Cell[kf->numRegisters];
}

StackFrame::StackFrame(const StackFrame &s) 
: call(s.call)
, caller(s.caller)
, kf(s.kf)
, callPathNode(s.callPathNode)
, allocas(s.allocas)
, minDistToUncoveredOnReturn(s.minDistToUncoveredOnReturn)
, varargs(s.varargs)
{
	if (s.locals == NULL) {
		locals = NULL;
		return;
	}

	locals = new Cell[s.kf->numRegisters];
	for (unsigned i=0; i<s.kf->numRegisters; i++)
		locals[i] = s.locals[i];
}

StackFrame::~StackFrame() { if (locals) delete[] locals; }

StackFrame& StackFrame::operator=(const StackFrame &s)
{
	Cell* new_locals;

	if (&s == this) return *this;

	if (s.locals != NULL) {
		new_locals = new Cell[s.kf->numRegisters];
		for (unsigned i=0; i<s.kf->numRegisters; i++)
			new_locals[i] = s.locals[i];
	} else
		new_locals = NULL;

	if (locals) delete[] locals;
	locals = new_locals;

	call = s.call;
	caller = s.caller;
	kf = s.kf;
	callPathNode = s.callPathNode;
	allocas = s.allocas;
	minDistToUncoveredOnReturn = s.minDistToUncoveredOnReturn;
	varargs = s.varargs;

	return *this;
}

void StackFrame::addAlloca(const MemoryObject* mo) { allocas.push_back(mo); }

bool StackFrame::clearLocals(void)
{
	if (locals == NULL) return false;
	delete [] locals;
	locals = NULL;
	return true;
}
