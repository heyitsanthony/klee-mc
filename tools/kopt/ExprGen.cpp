#include <iostream>
#include "klee/Internal/ADT/RNG.h"
#include "ExprGen.h"

using namespace klee;

#define OP_SZ	30
static Expr::Kind op_kinds[OP_SZ] =
{
	Expr::Read,
	Expr::Select,
	Expr::Concat,
	Expr::Extract,
	Expr::ZExt, Expr::SExt,
	Expr::Add, Expr::Sub, Expr::Mul,
	Expr::UDiv, Expr::SDiv, Expr::URem, Expr::SRem,
	Expr::Not, Expr::And, Expr::Or, Expr::Xor,
	Expr::Shl, Expr::LShr, Expr::AShr,
	Expr::Eq, Expr::Ne,
	Expr::Ult, Expr::Ule, Expr::Ugt, Expr::Uge,
	Expr::Slt, Expr::Sle, Expr::Sgt, Expr::Sge
};

ref<Expr> ExprGen::genExpr(
	ref<Expr> base, ref<Array> arr, unsigned ops)
{
	RNG	rng(rand());
	return genExpr(rng, base, arr, ops);
}


ref<Expr> ExprGen::genExpr(
	RNG& rng,
	ref<Expr> base, ref<Array> arr, unsigned ops)
{
	ref<Expr>	cur_expr;

	cur_expr = base;
	for (unsigned k = 0; k < ops; k++) {
		Expr::Kind		kind;
		std::vector<Expr::CreateArg>	v;

		kind = op_kinds[rng.getInt32() % OP_SZ];
		switch (kind) {
		case Expr::Read: {
			UpdateList ul(arr.get(), 0);
			cur_expr = ReadExpr::create(
				UpdateList(arr.get(), 0),
				ZExtExpr::create(
					ZExtExpr::create(cur_expr, 8),
					32));
			break;
		}

		case Expr::Select: {
			ref<Expr>	ce, cond;
			/* XXX randomize */
			ce = ConstantExpr::create(1, 1);
			ce = ZExtExpr::create(ce, cur_expr->getWidth());
			cond = EqExpr::create(ce, cur_expr);
			cur_expr = SelectExpr::create(
				cond,
				cur_expr,
				XorExpr::create(ce, cur_expr));
			break;
		}

		case Expr::Extract: {
			unsigned	off, w;

			off = rng.getInt32() % cur_expr->getWidth();
			w = cur_expr->getWidth() - off;
			assert (w > 0);
			w -= rng.getInt32() % w;

			cur_expr = ExtractExpr::create(cur_expr, off, w);
			break;
		}

		case Expr::Not:
			cur_expr = NotExpr::create(cur_expr);
			break;

		case Expr::ZExt:
		case Expr::SExt: {
			v.push_back(Expr::CreateArg(cur_expr));
			v.push_back(Expr::CreateArg((rng.getInt32() % 64)+1));
			cur_expr = Expr::createFromKind(kind, v);
			break;
		}

		case Expr::Concat:
			/* so that expr doesn't get huge width */
			if (cur_expr->getWidth() > 64)
				cur_expr = ZExtExpr::create(cur_expr, 32);

		case Expr::UDiv:
		case Expr::SDiv:
		case Expr::URem:
		case Expr::SRem:
			if (cur_expr->isZero()) {
				cur_expr = base;
				/* ugh just skip */
				if (cur_expr->isZero())
					break;
			}
		case Expr::Add:
		case Expr::Sub:
		case Expr::Mul:
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
		case Expr::Sge: {
			ref<Expr>	ce;
			unsigned int	n;
			/* XXX randomize */
			n = rng.getInt32();
			if (n == ~0 || n == 0) n = 2;

			ce = ConstantExpr::create(n, 32);
			ce = ZExtExpr::create(ce, cur_expr->getWidth());
			if (rng.getInt32() % 2)
				ce = NotExpr::create(ce);

			/* don't upset sdiv / udiv */
			/* TODO: do a (select (= 0 expr) 1 expr) for div/rem */
			if (ce->isZero()) {
				ce = ConstantExpr::create(1, 32);
				ce = ZExtExpr::create(ce, cur_expr->getWidth());
			}

			if (rng.getInt32() % 2) {
				v.push_back(Expr::CreateArg(ce));
				v.push_back(Expr::CreateArg(cur_expr));
			} else {
				v.push_back(Expr::CreateArg(cur_expr));
				v.push_back(Expr::CreateArg(ce));
			}
			cur_expr = Expr::createFromKind(kind, v);

			break;
		}

		default:
			std::cerr << "Whoops!\n";
			assert (0 == 1);
			break;
		}
	}

	return cur_expr;
}
