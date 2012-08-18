#include "../Expr/ShadowExpr.h"
#include "../Expr/ShadowAlloc.h"
#include "ShadowObjectState.h"

using namespace klee;

ShadowObjectState::ShadowObjectState(const ObjectState& os)
: UnboxingObjectState(os)
{
	const ShadowObjectState*	sos;

	sos = dynamic_cast<const ShadowObjectState*>(&os);
	if (sos == NULL) {
		/* should this *ever* happen? */
		is_tainted = false;
		tainted_bytes = 0;
		return;
	}

	is_tainted = sos->is_tainted;
	taint_v = sos->taint_v;
	tainted_bytes = sos->tainted_bytes;
}

#include <iostream>
void ShadowObjectState::write8(unsigned offset, ref<Expr>& value)
{
	/* XXX: this is imprecise */
	if (value->isShadowed())
		tainted_bytes++;

	if (value->getKind() != Expr::Constant) {
		ObjectState::write8(offset, value);
		return;
	}

	if (value->isShadowed()) {
		ObjectState::write8(offset, value);
		return;
	}

	UnboxingObjectState::write8(offset, value);
}

void ShadowObjectState::write(unsigned offset, const ref<Expr>& value)
{
	/* XXX: this is imprecise */
	if (value->isShadowed()) tainted_bytes++;

	if (value->getKind() != Expr::Constant) {
		ObjectState::write(offset, value);
		return;
	}

	if (value->isShadowed()) {
		ObjectState::write(offset, value);
		return;
	}

	UnboxingObjectState::write(offset, value);
}

void ShadowObjectState::taintAccesses(uint64_t v)
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
 
bool ShadowObjectState::isByteTainted(unsigned off) const
{ return read8(off)->isShadowed(); }
