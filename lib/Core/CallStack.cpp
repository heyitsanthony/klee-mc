#include "klee/Internal/Module/KFunction.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/CallStack.h"
#include "klee/util/Assignment.h"
#include "static/Sugar.h"

using namespace klee;

const Cell& CallStack::getTopCell(unsigned i) const
{ return getLocalCell(size() - 1, i); }

const Cell& CallStack::getLocalCell(unsigned sfi, unsigned i) const
{
	assert (sfi < size());

	const StackFrame	&sf(at(sfi));
	const KFunction		*kf(sf.kf);

	assert(i < kf->numRegisters);
	return sf.locals[i];
}

void CallStack::evaluate(const Assignment& a)
{
	foreach (it, rbegin(), rend()) {
		StackFrame	&sf(*it);

		if (sf.kf == NULL || sf.isClear())
			break;

		/* update all registers in stack frame */
		for (unsigned i = 0; i < sf.kf->numRegisters; i++) {
			ref<Expr>	e;

			if (sf.locals[i].value.isNull())
				continue;

			if (sf.locals[i].value->getKind() == Expr::Constant)
				continue;

			e = a.evaluate(sf.locals[i].value);
			sf.locals[i].value = e;
		}
	}
}

bool CallStack::hasLocal(const KInstruction *target) const
{
	if (empty()) return false;

	const StackFrame& sf(at(size() - 1));

	return (target->getDest() < sf.kf->numRegisters);
}

void CallStack::writeLocalCell(unsigned i, const ref<Expr>& value)
{ writeLocalCell(size() - 1, i, value); }

void CallStack::writeLocalCell(unsigned sfi, unsigned i, const ref<Expr>& value)
{
	assert(sfi < size());
	
	const StackFrame	&sf(at(sfi));
	const KFunction		*kf = sf.kf;

	assert(i < kf->numRegisters);

	sf.locals[i].value = value;
}


CallStack::insstack_ty CallStack::getKInstStack(void) const
{
	insstack_ty	ret;

	foreach (it, begin(), end()) {
		const KInstruction	*ki(((*it).caller));
		ret.push_back(ki);
	}

	return ret;
}

unsigned CallStack::clearTail(void)
{
	unsigned ret = 0;
	for (int i = size() - 2; i >= 0; i--) {
		if (at(i).isClear()) break;
		ret += at(i).kf->numRegisters;
	}
	return ret;
}
