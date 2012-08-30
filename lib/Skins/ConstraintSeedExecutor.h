#ifndef CONSTRAINT_SEED_EXE_H
#define CONSTRAINT_SEED_EXE_H

#include "../Core/Executor.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Expr.h"
#include "ConstraintSeedCore.h"

namespace klee
{
class ExecutionState;
class MemoryObject;
template<typename T>
class ConstraintSeedExecutor : public T
{
public:
	ConstraintSeedExecutor(InterpreterHandler* ie)
	: T(ie), csCore(this) {}
	virtual ~ConstraintSeedExecutor() {}

	virtual ObjectState* makeSymbolic(
		ExecutionState& state,
		const MemoryObject* mo,
		ref<Expr> len,
		const char* arrPrefix = "arr")
	{
		ObjectState*	os(T::makeSymbolic(state, mo, len, arrPrefix));
		if (os == NULL)
			return NULL;

		csCore.addSeedConstraints(state, os->getArrayRef());
		return os;
	}

	virtual void executeInstruction(
		ExecutionState &state, KInstruction *ki)
	{
		llvm::Instruction *i = ki->getInst();

		/* XXX: this is not ideal; I'd prefer to instrument the
		 * code with a special instruction which will give me the
		 * expression I'm interested in. */
		switch (i->getOpcode()) {
#define INST_DIVOP(x)		\
		case llvm::Instruction::x : {	\
		ref<Expr>	r(T::eval(ki, 1, state));	\
		if (r->getKind() == Expr::Constant) break;	\
		csCore.logConstraint(MK_EQ(MK_CONST(0, r->getWidth()), r));\
		break; }
		INST_DIVOP(UDiv)
		INST_DIVOP(SDiv)
		INST_DIVOP(URem)
		INST_DIVOP(SRem)
#undef INST_DIVOP
		}

		T::executeInstruction(state, ki);
	}

#if 0
	bool addConstraint(ExecutionState &state, ref<Expr> condition)
	{
		bool	ok;
		ok = T::addConstraint(state, condition);
		if (ok) return true;
		assert (0 == 1 && "STUB: remove superfluous constraints");
		return true;
	}
#endif

protected:
	virtual void xferIterInit(
		struct T::XferStateIter& iter,
		ExecutionState* state,
		KInstruction* ki)
	{
		T::xferIterInit(iter, state, ki);

		if (iter.v->getKind() == Expr::Constant)
			return;

		/* NOTE: alternatively, this could be some stack location
		 * we want to inject an expl017 into */
		csCore.logConstraint(
			SltExpr::create(
				iter.v,
				ConstantExpr::create(
					0x400000 /* program beginning */,
					iter.v->getWidth())));
	}

private:
	ConstraintSeedCore	csCore;
};
}
#endif
