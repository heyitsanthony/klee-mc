#include <iostream>
#include <assert.h>

#include "ExprPatternMatch.h"
#include "ExprRule.h"

using namespace klee;

uint64_t ExprPatternMatch::const_miss_c = 0;
uint64_t ExprPatternMatch::const_hit_c = 0;

bool ExprPatternMatch::verifyConstant(const ConstantExpr* ce)
{
	unsigned bits;

	bits = ce->getWidth();
	if (rule_it.matchValue(bits) == false)
		return false;

	if (bits <= 64)
		return rule_it.matchValue(ce->getZExtValue());

	/* handle large constants */
	while (bits) {
		unsigned		w;
		const ConstantExpr	*cur_ce;

		w = (bits > 64) ? 64 : bits;
		cur_ce = dyn_cast<ConstantExpr>(
			ExtractExpr::create(
				const_cast<ConstantExpr*>(ce),
				bits - w,
				w));
		if (cur_ce == NULL) {
			std::cerr << "[ExprMatch] EXPECTED CE FROM EXT(CE)!\n";
			return false;
		}

		if (rule_it.matchValue(cur_ce->getZExtValue()) == false)
			return false;

		bits -= w;
	}

	return true;
}

bool ExprPatternMatch::verifyConstant(uint64_t v, unsigned w)
{
	if (rule_it.matchValue(Expr::Constant) == false)
		return Stop;

	if (rule_it.matchValue(w) == false)
		return false;

	if (rule_it.matchValue(v) == false)
		return false;

	return true;
}

ExprPatternMatch::Action ExprPatternMatch::matchLabel(
	const Expr* expr, uint64_t label_op)
{
	labelmap_ty::iterator	it;
	uint64_t		label_num;

	label_num = OP_LABEL_NUM(label_op);

	it = lm.find(label_num);
	if (it == lm.end()) {
		/* create new label */
		lm[label_num] = ref<Expr>(const_cast<Expr*>(expr));
		goto skip_label;
	}

	/* does the expression correspond with last expr seen for label? */
	if (*it->second != *expr) {
		/* mismatch: one label, two expressions! */
		success = false;
		return Stop;
	}

skip_label:
	/* label was matched, continue */
	if (rule_it.isDone()) {
		success = true;
		return Stop;
	}

	return Skip;
}

ExprPatternMatch::Action ExprPatternMatch::matchCLabel(
	const Expr* expr, uint64_t label_op)
{
	labelmap_ty::iterator	it;
	uint64_t		label_num;

	/* width must match */
	if (rule_it.matchValue(expr->getWidth()) == false)
		return Stop;

	/* skip example value */
	if (rule_it.skipValue() == false)
		return Stop;

	label_num = OP_CLABEL_NUM(label_op);

	it = clm.find(label_num);
	if (it == clm.end()) {
		/* create new label */
		clm[label_num] = ref<Expr>(const_cast<Expr*>(expr));
		goto skip_label;
	}

	/* does the expression correspond with last expr seen for label? */
	if (*it->second != *expr) {
		/* mismatch: one label, two expressions! */
		success = false;
		return Stop;
	}

skip_label:
	/* label was matched, continue */
	if (rule_it.isDone()) {
		success = true;
		return Stop;
	}

	return Skip;
}

ExprPatternMatch::Action ExprPatternMatch::visitExpr(const Expr* expr)
{
	uint64_t	label_op;

	/* success => stop */
	assert (success == false);

	/* SUPER IMPORTANT: read labels are always 8-bits until otherwise
	 * noted. Permitting other sizes tends to wreck expected sizes. */
	if (expr->getWidth() == 8 || expr->getKind() == Expr::Constant) {
		bool	matched;

		matched = rule_it.matchLabel(label_op);
		if (matched) {
			/* read label */
			if (expr->getWidth() == 8 && OP_LABEL_TEST(label_op))
				return matchLabel(expr, label_op);

			/* handle slotted constants */
			if (expr->getKind() == Expr::Constant &&
				OP_CLABEL_TEST(label_op))
				return matchCLabel(expr, label_op);

			/* oops. */
			success = false;
			return Stop;
		}
	}

	/* match the expression node's opcode */
	if (rule_it.matchValue(expr->getKind()) == false) {
		success = false;
		return Stop;
	}

	switch (expr->getKind()) {
	case Expr::Constant:
		if (!verifyConstant(static_cast<const ConstantExpr*>(expr)))
			return Stop;
		break;
	case Expr::Select: break;
	case Expr::Concat: break;
	case Expr::Extract: {
		const ExtractExpr	*ex;

		ex = static_cast<const ExtractExpr*>(expr);
		if (!verifyConstant(ex->offset, 32))
			return Stop;

		if (!verifyConstant(ex->getWidth(), 32))
			return Stop;
		break;
	}

	case Expr::SExt:
	case Expr::ZExt:
		if (!verifyConstant(expr->getWidth(), 32))
			return Stop;
		break;

	case Expr::Add:
	case Expr::Sub:
	case Expr::Mul:
	case Expr::UDiv:
	case Expr::SDiv:
	case Expr::URem:
	case Expr::SRem:
	case Expr::Not:
	case Expr::And:
	case Expr::Or:
	case Expr::Xor:
	case Expr::Shl:
	case Expr::LShr:
	case Expr::AShr:
	case Expr::Eq:
	case Expr::Ne:
	case Expr::Ult:
	case Expr::Ule:
	case Expr::Ugt:
	case Expr::Uge:
	case Expr::Slt:
	case Expr::Sle:
	case Expr::Sgt:
	case Expr::Sge:
	break;
	default:
		std::cerr << "[ExprRule] Unknown op. Bailing\n";
		return Stop;
	}

	/* rule ran out of steam. we're done?? */
	if (rule_it.isDone()) {
		success = true;
		return Stop;
	}

	return Expand;
}

bool ExprPatternMatch::match(const ref<Expr>& e)
{
	const ExprRule	*er;

	success = false;
	lm.clear();
	rule_it.reset();
	apply(e);

	if (!success || clm.empty())
		return success;

	er = rule_it.getExprRule();
	success = er->checkConstants(clm);

	if (success)
		const_hit_c++;
	else
		const_miss_c++;

	return success;
}
