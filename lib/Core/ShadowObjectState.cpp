#include "../Expr/ShadowExpr.h"
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
