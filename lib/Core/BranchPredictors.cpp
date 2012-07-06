#include "klee/Internal/Module/KInstruction.h"
#include "BranchPredictors.h"
#include "klee/Internal/ADT/RNG.h"
#include "static/Sugar.h"

namespace klee { extern RNG theRNG; }

using namespace klee;


RandomPredictor::RandomPredictor()
: phase_hint(0)
, period(32)
, period_bump(2) {}

#include <iostream>
bool RandomPredictor::predict(const StateBranch& sb, bool& hint)
{
	hint = theRNG.getBool();
	//static unsigned c = 0;
	//std::cerr << "RAND PRED: " << c++ << ". HINT=" << hint << '\n';
	//std::cerr << "RAND COND: " << sb.cond << '\n';
	return true;
}

#define BIAS_WIDTH	8
bool ExprBiasPredictor::predict(const StateBranch& sb, bool& hint)
{
	const ConstantExpr	*ce;
	const EqExpr		*ee;

	ee = dyn_cast<EqExpr>(sb.cond);
	if (ee == NULL)
		return false;

	ce = dyn_cast<ConstantExpr>(ee->getKid(0));
	if (ce == NULL)
		return false;

	/* likely to be (eq false (x == y)) or something.
	 * this would induce a bias we don't want toward certain equalities. */
	if (ce->getWidth() == 1)
		return false;

	if (!ce->isZero()) {
		if (ce->getWidth() <= 64) {
			/* check for 0x0..0ffff masks */
			uint64_t	v;
			int		bitc = 0;
			v = ce->getZExtValue();
			v++;
			while (v && bitc < 2) {
				if (v & 1)
					bitc++;
				v >>= 1;
			}
			if (bitc > 1)
				return false;
		} else
			return false;
	}

	/* bias toward (Eq CONST x) being false */
	/* we are biasing *against* x == 0 and x == -1 */
	if ((theRNG.getInt32() % BIAS_WIDTH) == 0) {
		/* x == <CE> */
		hint = true;
	} else {
		/* x != <CE> */
		hint = false;
	}

	return true;
}

bool KBrPredictor::predict(const StateBranch& sb, bool& hint)
{
	KBrInstruction	*kbr;
	bool		fresh_false, fresh_true;

	kbr = static_cast<KBrInstruction*>(sb.ki);
	fresh_false = (kbr->hasFoundFalse() == false);
	fresh_true = (kbr->hasFoundTrue() == false);

	if (!fresh_false && !fresh_true) {
		if (kbr->getTrueFollows() == 0) {
			hint = true;
			return true;
		}

		if (kbr->getFalseFollows() == 0) {
			hint = false;
			return true;
		}

		return false;
	}

	if (fresh_false && fresh_true) {
		/* two-way branch-- flip coin to avoid bias */
		hint = theRNG.getBool();
	} else if (fresh_false)
		hint = false;
	else /* if (fresh_true) */
		hint = true;

	return true;
}

bool SeqPredictor::predict(const StateBranch& sb, bool& hint)
{
	hint = seq[idx++ % seq.size()];
	return true;
}

RotatingPredictor::RotatingPredictor()
: period(100)
, tick(0)
{}

RotatingPredictor::~RotatingPredictor()
{
	foreach (it, bps.begin(), bps.end())
		delete (*it);
}

bool RotatingPredictor::predict(const StateBranch& sb, bool& hint)
{
	return bps[(tick++ / period) % bps.size()]->predict(sb, hint);
}

#include "Forks.h"
bool CondPredictor::predict(const StateBranch& sb, bool& hint)
{
	/* XXX need to distinguish between forking conditions and
	 * conditions that have never been seen in the past */
	bool	has_true, has_false;

	has_true = f->hasSuccessor(sb.cond);
	has_false = f->hasSuccessor(Expr::createIsZero(sb.cond));

	if (!has_false && !has_true) {
		return false;
	}

	if (!has_true) {
		hint = true;
		return true;
	}

	if (!has_false) {
		hint = false;
		return true;
	}

	return false;
}

bool FollowedPredictor::predict(const StateBranch& sb, bool& hint)
{
	KBrInstruction	*kbr;

	kbr = static_cast<KBrInstruction*>(sb.ki);
	if (kbr->getTrueFollows() == kbr->getFalseFollows())
		return false;

	hint = (kbr->getTrueFollows() < kbr->getFalseFollows());
	return true;
}

bool SkewPredictor::predict(const StateBranch& sb, bool& hint)
{
	skewmap_ty::iterator	it;
	double			bias;

	predicts++;
	if ((predicts % 128) == 0)
		brSkews.clear();

	it = brSkews.find(sb.ki);
	if (it == brSkews.end()) {
		bias = theRNG.getDouble();
		brSkews.insert(std::make_pair(sb.ki, bias));
	} else
		bias = it->second;

	hint = theRNG.getDouble() < bias;
	return true;
}

