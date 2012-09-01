#ifndef CONSTRAINTSEEDCORE_H
#define CONSTRAINTSEEDCORE_H

#include <list>
#include <map>
#include <string>

#include "klee/Expr.h"

namespace klee
{
class Executor;
class ExecutionState;
class ExprVisitor;

class ConstraintSeedCore
{
public:
	typedef std::vector<ref<Expr> >			exprlist_ty;
	typedef std::map<std::string, exprlist_ty*>	name2exprs_ty;

	ConstraintSeedCore(Executor* _exe);
	virtual ~ConstraintSeedCore();
	void addSeedConstraints(ExecutionState& state, const ref<Array> arr);

	bool logConstraint(const ref<Expr> e);

	static bool isActive(void);
private:
	bool loadConstraintFile(const std::string& path);
	bool addExprToLabel(const std::string& s, const ref<Expr>& e);

	ref<Expr> getDisjunction(
		ExprVisitor*	ev,
		const exprlist_ty* el) const;

	ref<Expr> getConjunction(
		ExprVisitor*	ev,
		const exprlist_ty* el) const;

	bool isExprAdmissible(const exprlist_ty* el, const ref<Expr>& e);

	Executor		*exe;
	name2exprs_ty		name2exprs;
	std::set<Expr::Hash>	hashes;
};
}

#endif
