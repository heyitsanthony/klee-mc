#ifndef SHADOWMMU_H
#define SHADOWMMU_H

#include "../Core/MMU.h"
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
		ShadowAlloc	*sa = NULL;
		bool		ok;

		ok = base_mmu->exeMemOp(state, mop);
		if (!ok) return ok;

		if (mop.address->getKind() == Expr::Constant)
			return ok;

		if (mop.isWrite)
			return ok;

		sa = ShadowAlloc::get();
		sa->startShadow(ShadowValExpr::create(MK_CONST(1234567,32)));

		ref<Expr>	s_e(state.stack.readLocal(mop.target));

		/* XXX: why can't it just be top level? */
		// s_e = s_e->reallocTopLevel();
		s_e = s_e->realloc();
		assert (s_e->isShadowed());
		state.bindLocal(mop.target, s_e);

		assert (state.stack.readLocal(mop.target)->isShadowed());
		sa->stopShadow();

		return ok;
	}
private:
	MMU	*base_mmu;
};
}
#endif