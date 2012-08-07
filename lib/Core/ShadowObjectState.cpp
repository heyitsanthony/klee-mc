#include "../Expr/ShadowExpr.h"
#include "../Expr/ShadowAlloc.h"
#include "ShadowObjectState.h"

using namespace klee;

void ShadowObjectState::write8(unsigned offset, ref<Expr>& value)
{
	if (value->getKind() != Expr::Constant) {
		ObjectState::write8(offset, value);
		return;
	}

	if (dynamic_cast<ShadowExpr<Expr,uint64_t>* >(value.get()) != NULL) {
		ObjectState::write8(offset, value);
		return;
	}

	UnboxingObjectState::write8(offset, value);
}

void ShadowObjectState::write(unsigned offset, const ref<Expr>& value)
{
	if (value->getKind() != Expr::Constant) {
		ObjectState::write(offset, value);
		return;
	}

	if (dynamic_cast<ShadowExpr<Expr, uint64_t>* >(value.get()) != NULL) {
		ObjectState::write(offset, value);
		return;
	}

	UnboxingObjectState::write(offset, value);
}

void ShadowObjectState::taint(uint64_t v)
{
	is_tainted = true;
	taint_v = v;
}

ref<Expr> ShadowObjectState::read8(unsigned offset) const
{
	ref<Expr>	ret;

	if (is_tainted) ShadowAlloc::get()->startShadow(taint_v);
	ret = UnboxingObjectState::read8(offset);
	if (is_tainted) ShadowAlloc::get()->stopShadow();

	return ret;
}

ref<Expr> ShadowObjectState::read8(ref<Expr> offset) const
{
	ref<Expr>	ret;

	if (is_tainted) ShadowAlloc::get()->startShadow(taint_v);
	ret = UnboxingObjectState::read8(offset);
	if (is_tainted) ShadowAlloc::get()->stopShadow();

	return ret;
}
