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
, onRet(NULL)
, stackWatermark(0)
, allocas(NULL)
, minDistToUncoveredOnReturn(0)
, varargs(0)
{ locals = new Cell[kf->numRegisters]; }

StackFrame::StackFrame(const StackFrame &s) 
: call(s.call)
, caller(s.caller)
, kf(s.kf)
, callPathNode(s.callPathNode)
, onRet(s.onRet)
, onRet_expr(s.onRet_expr)
, stackWatermark(s.stackWatermark)
, allocas(NULL)
, minDistToUncoveredOnReturn(s.minDistToUncoveredOnReturn)
, varargs(s.varargs)
{
	if (this == &s)
		return;

	if (s.allocas != NULL) {
		allocas = new allocas_ty();
		*allocas = *s.allocas;
	}

	if (s.locals == NULL) {
		locals = NULL;
		return;
	}

	locals = new Cell[s.kf->numRegisters];
	for (unsigned i=0; i<s.kf->numRegisters; i++)
		locals[i] = s.locals[i];
}

StackFrame::~StackFrame()
{
	if (locals) delete[] locals;
	if (allocas) delete allocas;
}

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
	onRet = s.onRet;
	onRet_expr = s.onRet_expr;
	callPathNode = s.callPathNode;

	if (allocas) {
		delete allocas;
		allocas = NULL;
	}

	if (s.allocas) {
		allocas = new allocas_ty();
		*allocas = *s.allocas;
	}

	stackWatermark = s.stackWatermark;

	minDistToUncoveredOnReturn = s.minDistToUncoveredOnReturn;
	varargs = s.varargs;

	return *this;
}

void StackFrame::addAlloca(const MemoryObject* mo)
{
	if (allocas == NULL) allocas = new allocas_ty();
	allocas->push_back(mo);
}

bool StackFrame::clearLocals(void)
{
	if (locals == NULL) return false;
	delete [] locals;
	locals = NULL;
	return true;
}
