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

protected:
	virtual void instBranchConditional(
		ExecutionState& state, KInstruction* ki)
	{
		ref<Expr>	cond(T::eval(ki, 0, state));
		KBrInstruction	*kbr = static_cast<KBrInstruction*>(ki);

		T::instBranchConditional(state, ki);

		if (cond->getKind() == Expr::Constant)
			return;

		/* seen both branches? */
		if (kbr->hasFoundTrue() && kbr->hasFoundFalse())
			return;

		/* XXX: I think the kbr stuff is backwards, but
		 * the tests only work like this.
		 * Contradict */
		if (kbr->hasFoundTrue())
			csCore.logConstraint(cond);
		else
			csCore.logConstraint(MK_NOT(cond));
	}

	virtual void xferIterInit(
		struct T::XferStateIter& iter,
		ExecutionState* state,
		KInstruction* ki)
	{
		T::xferIterInit(iter, state, ki);

		if (iter.v->getKind() == Expr::Constant)
			return;

		/* NOTE: alternatively, this could be some stack location
		 * we want to inject an exploit into */
		csCore.logConstraint(
			MK_SLT(	iter.v,
				MK_CONST(0x400000 /* program beginning */,
					iter.v->getWidth())));
	}

private:
	ConstraintSeedCore	csCore;
};
}
#endif
