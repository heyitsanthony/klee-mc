#include <llvm/Support/CommandLine.h>
#include "static/Sugar.h"
#include "../../lib/Expr/ExprRule.h"
#include "KnockOut.h"

using namespace klee;
using namespace llvm;

namespace 
{
	cl::opt<unsigned>
	KOConsts("ko-consts", cl::desc("KO const widths"), cl::init(8));
}

KnockOut::Action KnockOut::visitConstant(const ConstantExpr& ce)
{
	ref<Expr>	new_expr;

	if (isIgnoreConst(ce))
		return Action::skipChildren();

	new_expr = Expr::createTempRead(arr, ce.getWidth(), arr_off);

	replvars.push_back(
		replvar_t(
			new_expr,
			const_cast<ConstantExpr*>(&ce)));
	arr_off += ce.getWidth() / 8;

	/* unconditional change-to */
	if (uniqReplIdx == -1)
		return Action::changeTo(new_expr);

	/* only accept *one* replacement into new expression */
	if (uniqReplIdx == ((int)replvars.size()-1))
		return Action::changeTo(new_expr);

	return Action::skipChildren();
}

KnockOut::Action KnockOut::visitExpr(const Expr& e)
{
	if (e.getKind() == Expr::NotOptimized)
		return Action::skipChildren();
	return ExprVisitor::visitExpr(e);
}

ref<Expr> KnockOut::apply(const ref<Expr>& e)
{
	arr_off = 0;
	replvars.clear();
	return visit(e);
}

bool KnockOut::isIgnoreConst(const ConstantExpr& ce)
{
	switch (KOConsts) {
	case 64:
		return (ce.getWidth() != 64);
	case 32:
		return (ce.getWidth() != 64 &&
			ce.getWidth() != 32);
	case 16:
		return (ce.getWidth() != 64 &&
			ce.getWidth() != 32 &&
			ce.getWidth() != 16);
	case 8:
		return (ce.getWidth() != 64 &&
			ce.getWidth() != 32 &&
			ce.getWidth() != 16 &&
			ce.getWidth() != 8);
	case 1:
		return (ce.getWidth() % 8 != 0 || ce.getWidth() > 64);
	default:
		assert (0 == 1);
	}
	return true;
}
