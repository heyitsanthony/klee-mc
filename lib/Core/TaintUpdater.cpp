#include "klee/ExecutionState.h"
#include "TaintUpdater.h"

using namespace klee;

#include <iostream>
ExecutionState& TaintUpdater::selectState(bool allowCompact)
{
	ExecutionState	*es;

	es = &baseSearcher->selectState(allowCompact);

	if (es != lastState) {
		/* recompute taint value */
		ref<Expr>	lt;
		ConstraintManager	cm(es->constraints - baseConstraints);
		SAVE_SHADOW
		_sa->stopShadow();
		if (cm.size() == 0) lt = MK_CONST(1, 1);
		else lt = BinaryExpr::Fold(Expr::And, cm.begin(), cm.end());
		POP_SHADOW
		lastTaint = ShadowAlloc::drop(lt);
		lastState = es;
	}

	ShadowAlloc::get()->startShadow(lastTaint);
	return *es;
}
