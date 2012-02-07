//===-- ImpliedValue.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ImpliedValue.h"

#include "Context.h"
#include "klee/Constraints.h"
#include "klee/Expr.h"
#include "klee/Solver.h"
#include "static/Sugar.h"
#include "klee/Internal/Support/IntEvaluation.h"

#include "klee/util/ExprUtil.h"

#include <iostream>
#include <map>
#include <set>

using namespace klee;

// XXX we really want to do some sort of canonicalization of exprs
// globally so that cases below become simpler
void ImpliedValue::getImpliedValues(
	ref<Expr> e,
	ref<ConstantExpr> value,
	ImpliedValueList &results)
{
	switch (e->getKind()) {
	case Expr::Constant:
		assert(	value == cast<ConstantExpr>(e) &&
			"error in implied value calculation");
		break;

	// Special
	case Expr::NotOptimized: break;

	case Expr::Read: {
		// XXX in theory it is possible to descend into a symbolic index
		// under certain circumstances (all values known, known value
		// unique, or range known, max / min hit). Seems unlikely this
		// would work often enough to be worth the effort.
		ReadExpr *re = cast<ReadExpr>(e);
		results.push_back(std::make_pair(re, value));
		break;
	}

	case Expr::Select: {
		// not much to do, could improve with range analysis
		SelectExpr *se = cast<SelectExpr>(e);
		ConstantExpr	*TrueCE, *FalseCE;

		TrueCE = dyn_cast<ConstantExpr>(se->trueExpr);
		if (TrueCE == NULL)
			break;

		FalseCE = dyn_cast<ConstantExpr>(se->falseExpr);
		if (FalseCE == NULL)
			break;

		if (TrueCE == FalseCE)
			break;

		if (value == TrueCE) {
			getImpliedValues(
				se->cond,
				ConstantExpr::alloc(1, Expr::Bool),
				results);
			break;
		}

		assert(	value == FalseCE &&
			"err in implied value calculation");
		getImpliedValues(
			se->cond,
			ConstantExpr::alloc(0, Expr::Bool),
			results);
		break;
	}

	case Expr::Concat: {
		ConcatExpr *ce = cast<ConcatExpr>(e);
		getImpliedValues(
			ce->getKid(0),
			value->Extract(
				ce->getKid(1)->getWidth(),
				ce->getKid(0)->getWidth()),
			results);
		getImpliedValues(
			ce->getKid(1),
			value->Extract(0, ce->getKid(1)->getWidth()),
			results);
		break;
	  }

	case Expr::Extract:
		// XXX, could do more here with "some bits" mask
		break;

	// Casting
	case Expr::ZExt:
	case Expr::SExt: {
		CastExpr *ce = cast<CastExpr>(e);
		getImpliedValues(
			ce->src,
			value->Extract(0, ce->src->getWidth()),
			results);
		break;
	}

	// Arithmetic

	case Expr::Add: { // constants on left
		BinaryExpr *be = cast<BinaryExpr>(e);
		// C_0 + A = C  =>  A = C - C_0
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(be->left))
			getImpliedValues(be->right, value->Sub(CE), results);
		break;
	}

	case Expr::Sub: { // constants on left
		BinaryExpr *be = cast<BinaryExpr>(e);
		// C_0 - A = C  =>  A = C_0 - C
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(be->left))
			getImpliedValues(be->right, CE->Sub(value), results);
		break;
	}

	case Expr::Mul: {
		// FIXME: Can do stuff here, but need valid mask and
		// other things because of bits that might be lost.
		break;
	}

	case Expr::UDiv:
	case Expr::SDiv:
	case Expr::URem:
	case Expr::SRem:
	break;

	// Binary
	case Expr::And: {
		BinaryExpr *be = cast<BinaryExpr>(e);

		if (be->getWidth() != Expr::Bool)
			break;

		if (value->isTrue() == false)
			break;

		getImpliedValues(be->left, value, results);
		getImpliedValues(be->right, value, results);

		// FIXME; We can propogate a mask here where we know "some bits".
		// May or may not be useful.
		break;
	}


	case Expr::Or: {
		BinaryExpr *be = cast<BinaryExpr>(e);

		if (value->isZero()) {
			ref<ConstantExpr>	z(
				ConstantExpr::create(0, e->getWidth()));

			getImpliedValues(be->left, z, results);
			getImpliedValues(be->right, z, results);
			break;
		}

		// FIXME: Can do more?
		break;
	}
	case Expr::Xor: {
		BinaryExpr *be = cast<BinaryExpr>(e);
		if (ConstantExpr *CE = dyn_cast<ConstantExpr>(be->left))
			getImpliedValues(be->right, value->Xor(CE), results);
		break;
	}

	// Comparison
	case Expr::Ne:
		value = value->Not();
		/* fallthru */
	case Expr::Eq: {
		EqExpr		*ee = cast<EqExpr>(e);
		ConstantExpr	*CE;

		CE = dyn_cast<ConstantExpr>(ee->left);
		if (value->isTrue()) {
			if (CE != NULL)
				getImpliedValues(ee->right, CE, results);
			break;
		}

		// Look for limited value range.
		//
		// In general anytime one side was restricted to
		// two values  we can apply this trick. The only
		// obvious case where this occurs, aside from
		// booleans, is as the result of a select expression
		// where the true and false branches are single valued
		// and distinct.

		if (CE != NULL)
			if (CE->getWidth() == Expr::Bool)
				getImpliedValues(ee->right, CE->Not(), results);
		break;
	}

	default:
		break;
	}
}

typedef std::map<ref<ReadExpr>, ref<ConstantExpr> > foundmap_ty;

static void getFoundMap(
	const ImpliedValueList	&results,
	foundmap_ty		&found)
{
	foreach (i, results.begin(), results.end()) {
		foundmap_ty::iterator it;

		it = found.find(i->first);
		if (it != found.end()) {
			assert(it->second == i->second &&
				"Invalid ImpliedValue!");
		} else {
			found.insert(std::make_pair(i->first, i->second));
		}
	}
}

typedef std::vector<ref<ReadExpr> >	readvec_ty;
typedef std::set<ref<ReadExpr> >	readset_ty;

ConstraintManager getBoundConstraints(
	ref<Expr>&		e,
	ref<ConstantExpr>&	value,
	const readvec_ty&	reads)
{
	std::vector<ref<Expr> > assumption;

	assumption.push_back(EqExpr::create(e, value));

	// obscure... we need to make sure that all the read indices are
	// bounds checked. if we don't do this we can end up constructing
	// invalid counterexamples because STP will happily make out of
	// bounds indices which will not get picked up. this is of utmost
	// importance if we are being backed by the CexCachingSolver.
	foreach (i, reads.begin(), reads.end()) {
		ReadExpr *re = i->get();
		assumption.push_back(
			UltExpr::create(
				re->index,
				ConstantExpr::alloc(
					re->updates.root->mallocKey.size,
					32)));
	}

	return ConstraintManager(assumption);
}


void ImpliedValue::checkForImpliedValues(
	Solver *S, ref<Expr> e, ref<ConstantExpr> value)
{
	readvec_ty		reads;
	foundmap_ty		found;
	ImpliedValueList	results;

	getImpliedValues(e, value, results);

	getFoundMap(results, found);


	ExprUtil::findReads(e, false, reads);
	readset_ty readsSet(reads.begin(), reads.end());
	reads = readvec_ty(readsSet.begin(), readsSet.end());

	ConstraintManager assume(getBoundConstraints(e, value, reads));

	foreach (i, reads.begin(), reads.end()) {
		std::map<ref<ReadExpr>, ref<ConstantExpr> >::iterator it;
		ref<ReadExpr>		var = *i;
		ref<ConstantExpr>	possible;
		bool			ok, res;

		ok = S->getValue(Query(assume, var), possible);
		assert(ok && "FIXME: Unhandled solver failure");

		ok = S->mustBeTrue(
			Query(assume, EqExpr::create(var, possible)), res);
		assert(ok && "FIXME: Unhandled solver failure");

		it = found.find(var);
		if (res) {
			if (it != found.end()) {
				assert(	possible == it->second &&
					"Invalid ImpliedValue!");
				found.erase(it);
			}
			continue;
		}

		if (it != found.end()) {
			ref<Expr> binding = it->second;
			std::cerr	<< "checkForImpliedValues: "
					<< e  << " = " << value << "\n"
					<< "\t\t implies "
					<< var << " == " << binding
					<< " (error)\n";
			assert(0);
		}
	}

	assert(found.empty());
}
