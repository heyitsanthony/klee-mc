#include <stdlib.h>
#include "klee/Constraints.h"
#include "klee/Query.h"
#include "klee/Solver.h"
#include "static/Sugar.h"
#include "RandomValue.h"

using namespace klee;

RandomValue::rvmap_ty RandomValue::rvmap;

bool RandomValue::get(Solver* s, const Query& q, ref<ConstantExpr>& result)
{
	rvmap_ty::iterator	it;
	RandomValue		*rv;

	it = rvmap.find(q.expr->hash());
	if (it == rvmap.end()) {
		rv = new RandomValue(q.expr->hash());
		rvmap.insert(std::make_pair(q.expr->hash(), rv));
	} else
		rv = it->second;

	return rv->getValue(s, q, result);
}

ref<ConstantExpr> RandomValue::tryExtend(Solver* s, const Query& q)
{
	ref<ConstantExpr>	result;

	if (s->getValueDirect(q, result) == false)
		return NULL;
	
	foreach (it, seen.begin(), seen.end())
		if (*it == result)
			return NULL;

	seen.push_back(result);

	if (min_res.isNull()) min_res = result;
	if (max_res.isNull()) max_res = result;

	if (MK_ULT(result, min_res)->isTrue())
		min_res = result;
	if (MK_UGT(result, max_res)->isTrue())
		max_res = result;

	return result;
}

ref<ConstantExpr> RandomValue::extend(Solver* s, const Query& q)
{
	if (seen.empty()) return tryExtend(s, q);

	ref<Expr>	unseen_cond(MK_CONST(1, 1)), trial_cond;
	bool		mbt;

	foreach (it, seen.begin(), seen.end())
		unseen_cond = MK_AND(unseen_cond, MK_NE(q.expr, *it));

	if (!s->mayBeTrue(q.withExpr(unseen_cond), mbt) || !mbt)
		return NULL;

	trial_cond = (rand() % 2)
		? MK_ULT(q.expr, min_res)
		: MK_UGT(q.expr, max_res);

	if (!s->mayBeTrue(q.withExpr(trial_cond), mbt) || !mbt)
		trial_cond = unseen_cond;

	ConstraintManager	new_cs(q.constraints);
	Query			new_q(new_cs, q.expr);

	new_cs.addConstraint(trial_cond);
	return tryExtend(s, new_q);
}

bool RandomValue::getValue(Solver* s, const Query& q, ref<ConstantExpr>& result)
{
	int	r;

	result = extend(s, q);
	if (!result.isNull())
		return true;

	r = rand() % seen.size();
	for (unsigned i = 0; i < seen.size(); i++) {
		bool	mbt;

		result = seen[(r+i) % seen.size()];
		if (!s->mayBeTrue(q.withExpr(MK_EQ(q.expr, result)), mbt))
			return false;

		if (mbt) return true;
	}

	return false;
}
