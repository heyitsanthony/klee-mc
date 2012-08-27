#ifndef SHADOWMMU_H
#define SHADOWMMU_H

#include "MMU.h"
#include "../Expr/ShadowAlloc.h"

namespace klee
{
class ShadowMMU : public MMU
{
public:
	ShadowMMU(MMU* _in) : MMU(_in->getExe()), base_mmu(_in) {}
	virtual ~ShadowMMU() { delete base_mmu; }

	virtual bool exeMemOp(ExecutionState &state, MemOp& mop)
	{
		bool		ok;

		ok = base_mmu->exeMemOp(state, mop);

		if (	ok && 
			mop.address->getKind() != Expr::Constant &&
			!mop.isWrite)
		{
			ShadowAlloc	*sa = NULL;
			sa = ShadowAlloc::get();
			sa->startShadow(ShadowValExpr::create(MK_CONST(1234567,32)));
			ref<Expr>	s_e(state.stack.readLocal(mop.target));
			s_e = s_e->realloc();
			assert (s_e->isShadowed());
			state.bindLocal(mop.target, s_e);

			assert (state.stack.readLocal(mop.target)->isShadowed());
			sa->stopShadow();
		}

		return ok;
	}
private:
	MMU	*base_mmu;
};
}
#endif