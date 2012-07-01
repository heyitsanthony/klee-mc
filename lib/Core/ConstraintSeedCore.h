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
class ConstraintSeedCore
{
public:
	typedef std::list<ref<Expr> >			exprlist_ty;
	typedef std::map<std::string, exprlist_ty*>	name2exprs_ty;

	ConstraintSeedCore(Executor* _exe);
	virtual ~ConstraintSeedCore();
	void addSeedConstraints(ExecutionState& state, const ref<Array> arr);
private:
	bool loadConstraintFile(const std::string& path);
	bool addExprToLabel(const std::string& s, const ref<Expr>& e);

	Executor	*exe;
	name2exprs_ty	name2exprs;
};
}

#endif
