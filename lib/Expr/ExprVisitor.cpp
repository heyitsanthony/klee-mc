//===-- ExprVisitor.cpp ---------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Expr.h"
#include "klee/util/ExprVisitor.h"

#include "llvm/Support/CommandLine.h"

#include <stack>
#include <iostream>

namespace {
  llvm::cl::opt<bool>
  UseVisitorHash("use-visitor-hash",
                 llvm::cl::desc("Use hash-consing during expr visitation."),
                 llvm::cl::init(true));
}

using namespace klee;

ref<Expr> ExprVisitor::Action::constantZero;

ExprVisitor::ExprVisitor(bool _recursive, bool in_visitConstants)
: recursive(_recursive)
, visitConstants(in_visitConstants)
{
	use_hashcons = UseVisitorHash;
}

ref<Expr> ExprVisitor::visit(const ref<Expr> &e)
{
	Expr		&ep(*e.get());
	ref<Expr>	res;
	Action 		a(Action::skipChildren());

	if (isa<ConstantExpr>(e)) {
		if (visitConstants)
			a = visitAction(ep);
	} else if (!use_hashcons) {
		a = visitAction(ep);
	} else {
		visited_ty::iterator it = visited.find(e);
		if (it != visited.end())
			return it->second;
		a = visitAction(ep);
	}

	switch(a.kind) {
	case Action::DoChildren:
		res = handleActionDoChildren(ep);
		break;
	case Action::SkipChildren: res = e; break;
	case Action::ChangeTo: res = a.argument; break;
	default:
		assert(0 && "invalid kind");
	}

	if (use_hashcons && !isa<ConstantExpr>(e))
		visited.insert(std::make_pair(e, res));

	return res;
}

ExprVisitor::Action ExprVisitor::visitAction(const Expr &ep)
{
	Action res = visitExpr(ep);
	switch(res.kind) {
	// continue with normal action
	case Action::DoChildren:
		break;
	case Action::SkipChildren:
	case Action::ChangeTo:
		return res;
	}

	switch(ep.getKind()) {
#define EXPR_CASE(x)	\
	case Expr::x: res = visit##x(static_cast<const x##Expr&>(ep)); break;

	EXPR_CASE(NotOptimized)
	EXPR_CASE(Read)
	EXPR_CASE(Select)
	EXPR_CASE(Concat)
	EXPR_CASE(Extract)
	EXPR_CASE(ZExt)
	EXPR_CASE(SExt)
	EXPR_CASE(Add)
	EXPR_CASE(Sub)
	EXPR_CASE(Mul)
	EXPR_CASE(UDiv)
	EXPR_CASE(SDiv)
	EXPR_CASE(URem)
	EXPR_CASE(SRem)
	EXPR_CASE(Not)
	EXPR_CASE(And)
	EXPR_CASE(Or)
	EXPR_CASE(Xor)
	EXPR_CASE(Shl)
	EXPR_CASE(LShr)
	EXPR_CASE(AShr)
	EXPR_CASE(Eq)
	EXPR_CASE(Ne)
	EXPR_CASE(Ult)
	EXPR_CASE(Ule)
	EXPR_CASE(Ugt)
	EXPR_CASE(Uge)
	EXPR_CASE(Slt)
	EXPR_CASE(Sle)
	EXPR_CASE(Sgt)
	EXPR_CASE(Sge)
	EXPR_CASE(Bind)
#undef EXPR_CASE

	case Expr::Constant:
		if (visitConstants) {
			res = visitConstant(
				static_cast<const ConstantExpr&>(ep));
			break;
		}
	default:
		assert(0 && "invalid expression kind");
	}

	return res;
}

ref<Expr> ExprVisitor::handleActionDoChildren(Expr& ep)
{
	bool rebuild = false;
	ref<Expr> e(&ep), kids[8];
	unsigned count = ep.getNumKids();

	assert (count < 8);
	for (unsigned i = 0; i < count; i++) {
		ref<Expr> kid = ep.getKid(i);
		kids[i] = visit(kid);
		if (kids[i] != kid)
			rebuild = true;
	}

	if (rebuild) {
		e = ep.rebuild(kids);
		if (recursive)
			e = visit(e);
	}

	if (visitConstants || !isa<ConstantExpr>(e)) {
		Action res = visitExprPost(*e.get());
		if (res.kind == Action::ChangeTo)
			e = res.argument;
	}
	return e;
}

ExprVisitor::Action ExprVisitor::visitExprPost(const Expr&)
{ return Action::skipChildren(); }

// simplify updates and build stack for rebuilding update list;
// we also want to see if there's a single value that would result from
// the read expression at this index.
// (e.g., reading at index X, and all updates following and
//  including an update to index X hold value Y, then the read X
//  will always return Y.)
//
// NOTE (bug fix): if there's not an update definitively at index X,
// then the entire Read must be sent to STP,
// as the index may read from the root array.
ref<Expr> ExprVisitor::buildUpdateStack(
	const UpdateList	&ul,
	ref<Expr>		&readIndex,
	std::stack<std::pair<ref<Expr>, ref<Expr> > >& updateStack,
	bool	&rebuildUpdates)
{
	ref<Expr>	uniformValue(0);
	bool		uniformUpdates = true;

	rebuildUpdates = false;
	for (const UpdateNode *un = ul.head; un != NULL; un=un->next) {
		ref<Expr> index = isa<ConstantExpr>(un->index)
			? un->index
			: visit(un->index);

		ref<Expr> value = isa<ConstantExpr>(un->value)
			? un->value
			: visit(un->value);

		// skip constant updates that cannot match
		// (!= symbolic updates could match!)
		// use EqExpr::create to utilize expr rewrite
		// rules for Selects, etc.
		if (EqExpr::create(readIndex, index)->isFalse()) {
			rebuildUpdates = true;
			continue;
		}

		if (index != un->index || value != un->value) {
			rebuildUpdates = true;
			uniformUpdates = false;
		} else if (uniformUpdates && uniformValue.isNull())
			uniformValue = value;
		else if (uniformUpdates && value != uniformValue)
			uniformUpdates = false;

		updateStack.push(std::make_pair(index, value));

		// if we have a satisfying update, terminate update list
		if (readIndex == index) {
			if (uniformUpdates && !uniformValue.isNull())
				return uniformValue;
			break;
		}
	}

	return NULL;
}



#define DO_CHILDREN_ACTION(x,y)					\
ExprVisitor::Action ExprVisitor::visit##x(const y&) { 		\
	return Action::doChildren(); }				\

DO_CHILDREN_ACTION(Expr, Expr)
DO_CHILDREN_ACTION(NotOptimized, NotOptimizedExpr)
DO_CHILDREN_ACTION(Read, ReadExpr)
DO_CHILDREN_ACTION(Select, SelectExpr)
DO_CHILDREN_ACTION(Concat, ConcatExpr)
DO_CHILDREN_ACTION(Extract, ExtractExpr)
DO_CHILDREN_ACTION(ZExt, ZExtExpr)
DO_CHILDREN_ACTION(SExt, SExtExpr)
DO_CHILDREN_ACTION(Add, AddExpr)
DO_CHILDREN_ACTION(Sub, SubExpr)
DO_CHILDREN_ACTION(Mul, MulExpr)
DO_CHILDREN_ACTION(UDiv, UDivExpr)
DO_CHILDREN_ACTION(SDiv, SDivExpr)
DO_CHILDREN_ACTION(URem, URemExpr)
DO_CHILDREN_ACTION(SRem, SRemExpr)
DO_CHILDREN_ACTION(Not, NotExpr)
DO_CHILDREN_ACTION(And, AndExpr)
DO_CHILDREN_ACTION(Or, OrExpr)
DO_CHILDREN_ACTION(Xor, XorExpr)
DO_CHILDREN_ACTION(Shl, ShlExpr)
DO_CHILDREN_ACTION(LShr, LShrExpr)
DO_CHILDREN_ACTION(AShr, AShrExpr)
DO_CHILDREN_ACTION(Eq, EqExpr)
DO_CHILDREN_ACTION(Ne, NeExpr)
DO_CHILDREN_ACTION(Ult, UltExpr)
DO_CHILDREN_ACTION(Ule, UleExpr)
DO_CHILDREN_ACTION(Ugt, UgtExpr)
DO_CHILDREN_ACTION(Uge, UgeExpr)
DO_CHILDREN_ACTION(Slt, SltExpr)
DO_CHILDREN_ACTION(Sle, SleExpr)
DO_CHILDREN_ACTION(Sgt, SgtExpr)
DO_CHILDREN_ACTION(Sge, SgeExpr)
DO_CHILDREN_ACTION(Bind, BindExpr)
DO_CHILDREN_ACTION(Constant, ConstantExpr)


void ExprConstVisitor::apply(const ref<Expr>& expr)
{ apply(expr.get()); }

#define OPEN_EXPR(x)	exprvis_ty(x, true)
#define CLOSE_EXPR(x)	exprvis_ty(x, false)
#define IS_OPEN_EXPR(x)		(x.second == true)
#define IS_CLOSE_EXPR(x)	(x.second == false)

void ExprConstVisitor::apply(const Expr* expr)
{
	std::stack<exprvis_ty>	expr_stack;

	expr_stack.push(OPEN_EXPR(expr));
	while (!expr_stack.empty()) {
		if (!processHead(expr_stack))
			break;
	}
}

bool ExprConstVisitor::processHead(std::stack<exprvis_ty>& expr_stack)
{
	exprvis_ty		cur_vis(expr_stack.top());
	const Expr*		cur_expr(cur_vis.first);
	Action			action;
	unsigned		num_kids;

	expr_stack.pop();

	if (IS_CLOSE_EXPR(cur_vis)) {
		visitExprPost(cur_expr);
		return true;
	}

	action = visitExpr(cur_expr);

	/* like skip, but no post expr */
	if (action == Close)
		return true;

	expr_stack.push(CLOSE_EXPR(cur_expr));

	if (action == Skip)
		return true;

	if (action == Stop)
		return false;

	assert (action == Expand);

	num_kids = cur_expr->getNumKids();
	for (int i = num_kids-1; i >= 0; i--) {
		/* preference to left side-- since we 
		 * prefer const-left linear exprs, we want to handle the
		 * left branch first; it's likely to be a leaf */
		expr_stack.push(OPEN_EXPR(cur_expr->getKid(i).get()));
	}

	/* Update nodes are special but still have
	 * expressions! Normal ExprVisitor ignores these. YucK!!! */
	const ReadExpr* re;
	if (	visit_update_lists &&
		(re = dyn_cast<const ReadExpr>(cur_expr)))
	{
		for (	const UpdateNode* un = re->updates.head;
			un != NULL;
			un = un->next)
		{
			expr_stack.push(OPEN_EXPR(un->index.get()));
			expr_stack.push(OPEN_EXPR(un->value.get()));
		}
	}

	return true;
}
