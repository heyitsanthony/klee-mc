/* the data dependency tainting executor */
/* Tainting algorithm:
 * Values are tainted with a function set of all functions
 * which introduced a data dependency.
 *
 * Loading from tainted pointer:
 * 	mix value and pointer taint; ...
 * Storing to tainted pointer:
 * 	mix value and pointer taint; store
 * Untainted value store:
 * 	taint value with function
 * Operations between two tainted values:
 * 	Taint result with union of function sets
 *
 * Base tainting comes from ???
 */


#ifndef DDTEXECUTOR_H
#define DDTEXECUTOR_H

#include "klee/Common.h"
#include "../Core/MMU.h"
#include "klee/Internal/Module/KModule.h"
#include "../Core/ExeStateManager.h"
#include "DDTCore.h"
#include "../Expr/ShadowAlloc.h"

namespace klee
{

class ExecutionState;
class InterpreterHandler;

template<typename T>
class DDTExecutor : public T
{
public:
	DDTExecutor(InterpreterHandler* ie)
	: T(ie), ddtCore() {}
	virtual ~DDTExecutor() {}

protected:
	void executeInstruction(ExecutionState &state,
				KInstruction *ki) override {
		unsigned	op;

		op = ki->getInst()->getOpcode();
		if (op == llvm::Instruction::Store) {
			ref<Expr>	base(T::eval(ki, 1, state));
			ref<Expr>	value(T::eval(ki, 0, state));

			value = ddtCore.mixLeft(value, base);
			MMU::MemOp	mop(true, base, value, 0);
			T::mmu->exeMemOp(state, mop);
		} else if (op == llvm::Instruction::Load) {
			ref<Expr> 	base(T::eval(ki, 0, state));
			MMU::MemOp	mop(false, base, 0, ki);
			
			PUSH_SHADOW(ShadowAlloc::getExprShadow(base));
			T::mmu->exeMemOp(state, mop);		
			POP_SHADOW
		} else
			T::executeInstruction(state, ki);
	}

	void executeGetValue(	ExecutionState &state,
				ref<Expr> e,
				KInstruction *target,
				ref<Expr> pred) override {
		assert (e->isShadowed() == false);
		assert(pred.isNull());
		T::executeGetValue(state, e, target);
	}

	llvm::Function* getFuncByAddr(uint64_t addr) override {
		unsigned	kf_c = T::kmodule->getNumKFuncs();
		llvm::Function	*f;

		f = T::getFuncByAddr(addr);

		if (T::kmodule->getNumKFuncs() > kf_c)
			ddtCore.taintFunction(f);

		return f;
	}
private:
	DDTCore	ddtCore;
};
};

#endif