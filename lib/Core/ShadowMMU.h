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
			sa->startShadow(1234567);
			ref<Expr>	s_e(state.readLocal(mop.target)->realloc());

			assert (s_e->isShadowed());
			state.bindLocal(mop.target, s_e);

			assert (state.readLocal(mop.target)->isShadowed());
			sa->stopShadow();
		}

		return ok;
	}
private:
	MMU	*base_mmu;
};
}
#endif