//===-- Constraints.cpp ---------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "llvm/Support/CommandLine.h"
#include "klee/Constraints.h"
#include "klee/Common.h"

#include "klee/util/Assignment.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprVisitor.h"
#include "static/Sugar.h"
#include "ExprReplaceVisitor.h"

#include <stack>
#include <string.h>
#include <iostream>
#include <list>
#include <map>

using namespace klee;

#define MAX_UPDATE_TIME	1000
#define MAX_REPL_SIZE	1000

namespace {
	llvm::cl::opt<bool>
	SimplifyUpdates(
		"simplify-updates",
		llvm::cl::desc(
		"Simplifies update list expressions in constraint manager."),
		llvm::cl::init(true));
}

unsigned ConstraintManager::simplify_c = 0;
unsigned ConstraintManager::simplified_c = 0;
unsigned ConstraintManager::timeout_c = 0;

namespace klee
{

class ExprReplaceVisitor2 : public ExprVisitor
{
public:
//	typedef std::tr1::unordered_map< ref<Expr>, ref<Expr> > replmap_ty;
	typedef std::map< ref<Expr>, ref<Expr> > replmap_ty;
	ExprReplaceVisitor2(void) : ExprVisitor(true)
	{ }

	ExprReplaceVisitor2(const replmap_ty& _replacements)
	: ExprVisitor(true)
	, replacements(_replacements) {}

	virtual ref<Expr> apply(const ref<Expr>& e)
	{
		if (replacements.size() > MAX_REPL_SIZE)
			replacements.clear();
		return ExprVisitor::apply(e);
	}

	Action visitExprPost(const Expr &e)
	{
		replmap_ty::const_iterator it;
		it = replacements.find(ref<Expr>(const_cast<Expr*>(&e)));
		if (it == replacements.end())
			return Action::doChildren();

		ConstraintManager::incReplacements();
		return Action::changeTo(it->second);
	}

	replmap_ty& getReplacements(void) { return replacements; }

protected:
	virtual Action visitRead(const ReadExpr &re);

private:
	replmap_ty replacements;
};

}

ExprVisitor::Action ExprReplaceVisitor2::visitRead(const ReadExpr &re)
{
	std::stack<std::pair<ref<Expr>, ref<Expr> > > updateStack;
	ref<Expr>		uniformValue(0);
	bool			rebuild, rebuildUpdates;
	const UpdateList	&ul(re.updates);

	// fast path: no updates, reading from constant array
	// with a single value occupying all indices in the array
	if (!ul.head && ul.getRoot()->isSingleValue()) {
		ConstraintManager::incReplacements();
		return Action::changeTo(ul.getRoot()->getValue(0));
	}

	ref<Expr> readIndex = isa<ConstantExpr>(re.index)
		? re.index
		: visit(re.index);

	// simplify case of a known read from a constant array
	if (	isa<ConstantExpr>(readIndex) &&
		!ul.head && ul.getRoot()->isConstantArray())
	{
		uint64_t idx;
		
		ConstraintManager::incReplacements();

		idx = cast<ConstantExpr>(readIndex)->getZExtValue();
		if (idx < ul.getRoot()->mallocKey.size)
			return Action::changeTo(ul.getRoot()->getValue(idx));

		std::cerr << "[Constraints] WTF: ARR=" << ul.getRoot()->name
			<< ". BAD IDX="
			<< idx << ". Size="
			<<  ul.getRoot()->mallocKey.size << '\n';
		klee_warning_once(
			0,
			"out of bounds constant array read (possibly within "
			"an infeasible Select path?)");

		return Action::changeTo(MK_CONST(0, re.getWidth()));
	}

	/* rebuilding fucks up symbolics on underconstrained exe */
	if (SimplifyUpdates == false)
		return Action::doChildren();

	rebuild = rebuildUpdates = false;
	if (readIndex != re.index)
		rebuild = true;

	uniformValue = buildUpdateStack(
		re.updates, readIndex, updateStack, rebuildUpdates);
	if (!uniformValue.isNull()) {
		ConstraintManager::incReplacements();
		return Action::changeTo(uniformValue);
	}

	if (rebuild && !rebuildUpdates) {
		ConstraintManager::incReplacements();
		return Action::changeTo(
			ReadExpr::create(re.updates, readIndex));
	}


	// at least one update was simplified? rebuild
	if (rebuildUpdates) {
		UpdateList	*newUpdates;
		ref<Expr>	new_re;

		newUpdates = UpdateList::fromUpdateStack(
			re.updates.getRoot().get(), updateStack);
		if (newUpdates == NULL)
			return Action::doChildren();

		new_re = ReadExpr::create(*newUpdates, readIndex);
		delete newUpdates;

		ConstraintManager::incReplacements();
		return Action::changeTo(new_re);
	}


	return Action::doChildren();
}

bool ConstraintManager::rewriteConstraints(ExprVisitor &visitor)
{
	ConstraintManager::constraints_ty old;
	bool changed = false;

	constraints.swap(old);
	invalidateSimplifier();

	assert (!Expr::errors);

	foreach (it, old.begin(), old.end()) {
		ref<Expr> &ce = *it;
		ref<Expr> e = visitor.apply(ce);

		/* ulp. */
		if (Expr::errors)
			break;

		if (!e.isNull() && e != ce) {
			// enable further reductions
			addConstraintInternal(e);
			changed = true;
			continue;
		}

		assert (!Expr::errors);
		if (changed)
			e = simplifyExpr(ce);
		constraints.push_back(ce);

		assert (!Expr::errors);
		invalidateSimplifier();
	}

	return changed;
}

void ConstraintManager::invalidateSimplifier(void) const
{
	if (!simplifier) return;
	delete simplifier;
	simplifier = NULL;
}

void ConstraintManager::simplifyForValidConstraint(ref<Expr> e)
{
	assert (0 == 1 && "STUB");
}

#define TRUE_EXPR	ConstantExpr::alloc(1, Expr::Bool)
#define FALSE_EXPR	ConstantExpr::alloc(0, Expr::Bool)

static void addEquality(
	ExprReplaceVisitor2::replmap_ty& equalities,
	const ref<Expr>& e)
{
	if (const EqExpr *ee = dyn_cast<EqExpr>(e)) {
		if (isa<ConstantExpr>(ee->left)) {
			equalities.insert(std::make_pair(ee->right, ee->left));
		} else {
			equalities.insert(std::make_pair(e, TRUE_EXPR));
		}
		return;
	}

	equalities.insert(std::make_pair(e, TRUE_EXPR));

	// DAR: common simplifications that make referent tracking on symbolics
	// more efficient. Collapses the constraints created by
	// compareOpReferents into simpler constraints. This is needed because
	// expression canonicalization turns everything into < or <=
	if (const UltExpr *x = dyn_cast<UltExpr>(e)) {
		equalities.insert(
			std::make_pair(
				MK_ULE(x->getKid(1), x->getKid(0)),
				FALSE_EXPR));
		return;
	}

	if (const UleExpr *x = dyn_cast<UleExpr>(e)) {
		equalities.insert(
			std::make_pair(
				MK_ULT(x->getKid(1), x->getKid(0)),
				FALSE_EXPR));

		// x <= 0 implies x == 0
		if (x->getKid(1)->isZero())
			equalities.insert(
				std::make_pair(x->getKid(0), x->getKid(1)));

		return;
	}
}

void ConstraintManager::setupSimplifier(void) const
{
	assert (simplifier == NULL);
	simplifier = new ExprTimer<ExprReplaceVisitor2>(MAX_UPDATE_TIME);
	ExprReplaceVisitor2::replmap_ty &equalities(simplifier->getReplacements());

	foreach (it, constraints.begin(), constraints.end())
		addEquality(equalities, *it);
}

ref<Expr> ConstraintManager::simplifyExpr(ref<Expr> e) const
{
	ref<Expr>	ret;

	if (isa<ConstantExpr>(e)) return e;

	if (simplifier == NULL)
		setupSimplifier();

	assert (!Expr::errors);
	simplify_c++;
	ret = simplifier->apply(e);
	if (ret.isNull()) {
		timeout_c++;
		return e;
	}

	if (ret->hash() != e->hash())
		simplified_c++;

	if (Expr::errors) {
		std::cerr << "CONSTRAINT MANAGER RUINED EXPR!!!!!!\n";
		Expr::resetErrors();
		return e;
	}

	return ret;
}

bool ConstraintManager::addConstraintInternal(ref<Expr> e)
{
	assert (!Expr::errors);
	// rewrite any known equalities, return false if
	// we find ourselves with a contradiction. This means that
	// the constraint we're adding can't happen!

	// XXX should profile the effects of this and the overhead.
	// traversing the constraints looking for equalities is hardly the
	// slowest thing we do, but it is probably nicer to have a
	// ConstraintSet ADT which efficiently remembers obvious patterns
	// (byte-constant comparison).
	switch (e->getKind()) {
	case Expr::Constant:
	//	assert(cast<ConstantExpr>(e)->isTrue() &&
	//	"attempt to add invalid (false) constraint");

		if (!cast<ConstantExpr>(e)->isTrue())
			return false;
		return true;

	// split to enable finer grained independence and other optimizations
	case Expr::And: {
		BinaryExpr *be = cast<BinaryExpr>(e);
		if (!addConstraintInternal(be->left)) return false;
		if (!addConstraintInternal(be->right)) return false;
		return true;
	}

	case Expr::Eq: {
		BinaryExpr *be = cast<BinaryExpr>(e);
		if (isa<ConstantExpr>(be->left)) {
			ExprTimer<ExprReplaceVisitor> visitor(
				be->right, be->left, MAX_UPDATE_TIME);
			rewriteConstraints(visitor);
		}
		constraints.push_back(e);
		invalidateSimplifier();
		return true;
	}

	default:
		constraints.push_back(e);
		invalidateSimplifier();
		return true;
	}

	return true;
}

bool ConstraintManager::addConstraint(ref<Expr> e)
{
	bool	added;

	e = simplifyExpr(e);
	assert(!Expr::errors);

	added = addConstraintInternal(e);
	if (Expr::errors) {
		Expr::resetErrors();
		return false;
	}

	return added;
}

void ConstraintManager::print(std::ostream& os) const
{
	for (unsigned int i = 0; i < constraints.size(); i++) {
		constraints[i]->print(os);
		os << "\n";
	}
}

ConstraintManager::~ConstraintManager(void)
{ if (simplifier) delete simplifier; }

bool ConstraintManager::isValid(const Assignment& a) const
{ return a.satisfies(begin(), end()); }
