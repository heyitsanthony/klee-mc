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
#include "AddressSpace.h"
#include "Memory.h"
#include "../Expr/ExprReplaceVisitor.h"
#include "klee/util/ExprUtil.h"
#include "klee/Internal/Module/KFunction.h"

#include <iostream>
#include <map>
#include <set>

using namespace klee;

uint64_t ImpliedValue::ivc_mem_bytes = 0;
uint64_t ImpliedValue::ivc_stack_cells = 0;

// XXX we really want to do some sort of canonicalization of exprs
// globally so that cases below become simpler
// XXX this recursive mess can be converted to use one of the nice
// expr crawlers that won't blow up at large depths.
void ImpliedValue::getImpliedValues(
	ref<Expr> e,
	const ref<ConstantExpr> value,
	ImpliedValueList &results)
{
	switch (e->getKind()) {
	case Expr::Constant:
		if(e != value) {
		std::cerr << "num results ivc: " << results.size() << '\n';
		for (const auto &res : results) {
			std::cerr << res.first << "==" << res.second << '\n';
		}
		std::cerr << "value: " << value << " vs " << e << '\n';
		}
		assert(	e == value &&
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
		SelectExpr		*se = cast<SelectExpr>(e);
		ref<ConstantExpr>	TrueCE, FalseCE;

		TrueCE = dyn_cast<ConstantExpr>(se->trueExpr);
		FalseCE = dyn_cast<ConstantExpr>(se->falseExpr);
		if (TrueCE.isNull() || FalseCE.isNull())
			break;

		if (TrueCE == FalseCE)
			break;

		if (value == TrueCE) {
			getImpliedValues(
				se->cond, MK_CONST(1, Expr::Bool), results);
			break;
		}

		assert(	value == FalseCE &&
			"err in implied value calculation");

		getImpliedValues(se->cond, MK_CONST(0, Expr::Bool), results);
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
			// (x | y) == 0 => x == y == 0
			getImpliedValues(be->left, value, results);
			getImpliedValues(be->right, value, results);
			break;
		}

		// FIXME: Can do more?
		// Could do byte-level analysis (bit-level won't yield much)
		// byte0 | byte1 == 0 => byte0 == byte1 == 0
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
		const auto	ee = cast<EqExpr>(e);
		const auto	CE = dyn_cast<ConstantExpr>(ee->left);
		bool		isTrue;

		if (CE == nullptr) {
			break;
		}

		isTrue = value->isTrue();
		if (isTrue) {
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
		assert(!isTrue);
		if (CE->getWidth() == Expr::Bool) {
			getImpliedValues(ee->right, CE->Not(), results);
			break;
		}

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
	for (const auto &result : results) {
		auto it = found.find(result.first);
		if (it != found.end()) {
			assert(it->second == result.second &&
				"Invalid ImpliedValue!");
		} else {
			found.insert(result);
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
	for (const auto &r : reads) {
		ReadExpr *re = r.get();
		assumption.push_back(
			MK_ULT(
				re->index,
				MK_CONST(
					re->updates.getRoot()->mallocKey.size,
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

	assert (S != NULL && "expected solver");

	getImpliedValues(e, value, results);

	getFoundMap(results, found);

	ExprUtil::findReads(e, false, reads);
	readset_ty readsSet(reads.begin(), reads.end());
	reads = readvec_ty(readsSet.begin(), readsSet.end());

	ConstraintManager assume(getBoundConstraints(e, value, reads));

	for (const auto &var : reads) {
		ref<ConstantExpr>	possible;
		bool			ok, res;

		ok = S->getValue(Query(assume, var), possible);
		assert(ok && "FIXME: Unhandled solver failure");

		ok = S->mustBeTrue(Query(assume, MK_EQ(var, possible)), res);
		assert(ok && "FIXME: Unhandled solver failure");

		auto it = found.find(var);
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
			abort();
		}
	}

	assert(found.empty());
}

void ImpliedValue::ivcMem(
	AddressSpace& as,
	const ref<Expr>& re, const ref<ConstantExpr>& ce)
{
	ExprReplaceVisitor	erv(re, ce);

	for (const auto &op : as) {
		const ObjectState	*os = op.second;
		ObjectState		*wos = NULL;

		if (os->isConcrete())
			continue;

		for (unsigned i = 0; i < os->getSize(); i++) {
			if (os->isByteKnownSymbolic(i) == false)
				continue;

			auto e = os->read8(i);
			auto e_new = erv.apply(e);
			if (e->hash() == e_new->hash())
				continue;

			if (wos == NULL) {
				wos = as.getWriteable(op);
				os = wos;
			}
			wos->write(i, e_new);
			ivc_mem_bytes++;
		}
	}
}

void ImpliedValue::ivcStack(
	CallStack& stk,
	const ref<Expr>& re, const ref<ConstantExpr>& ce)
{
	for (auto &sf : stk) {
		ExprReplaceVisitor	erv(re, ce);

		if (sf.kf == NULL || sf.isClear())
			continue;

		/* update all registers in stack frame */
		for (unsigned i = 0; i < sf.kf->numRegisters; i++) {
			ref<Expr>	e, old_v(sf.locals[i].value);

			if (old_v.isNull() || old_v->getKind() == Expr::Constant)
				continue;

			e = erv.apply(old_v);
			if (e->hash() == old_v->hash())
				continue;

			ivc_stack_cells++;
			sf.locals[i].value = e;
		}
	}
}
