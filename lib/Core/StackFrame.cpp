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
, onRet(NULL)
, stackWatermark(0)
, locals(std::make_unique<Cell[]>(kf->numRegisters))
, minDistToUncoveredOnReturn(0)
, varargs(0)
{}

StackFrame::StackFrame(const StackFrame &s) 
: call(s.call)
, caller(s.caller)
, kf(s.kf)
, onRet(s.onRet)
, onRet_expr(s.onRet_expr)
, stackWatermark(s.stackWatermark)
, minDistToUncoveredOnReturn(s.minDistToUncoveredOnReturn)
, varargs(s.varargs)
{
	if (this == &s)
		return;

	if (s.allocas) {
		allocas = std::make_unique<allocas_ty>(*s.allocas);
	}

	if (s.locals) {
		locals = std::make_unique<Cell[]>(s.kf->numRegisters);
		for (unsigned i = 0; i < s.kf->numRegisters; i++)
			locals[i] = s.locals[i];
	}
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

	locals.reset(new_locals);

	call = s.call;
	caller = s.caller;
	kf = s.kf;
	onRet = s.onRet;
	onRet_expr = s.onRet_expr;

	if (allocas) allocas.reset();

	if (s.allocas) {
		allocas = std::make_unique<allocas_ty>(*s.allocas);
	}

	stackWatermark = s.stackWatermark;

	minDistToUncoveredOnReturn = s.minDistToUncoveredOnReturn;
	varargs = s.varargs;

	return *this;
}

void StackFrame::addAlloca(const MemoryObject* mo)
{
	if (!allocas) allocas = std::make_unique<allocas_ty>();
	allocas->push_back(mo);
}

bool StackFrame::clearLocals(void)
{
	if (!locals) return false;
	locals.reset();
	return true;
}
