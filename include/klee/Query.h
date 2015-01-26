#ifndef QUERY_H
#define QUERY_H

namespace klee
{
class ConstraintManager;

class Query
{
public:
	const ConstraintManager &constraints;
	ref<Expr> expr;

	Query(const ConstraintManager& _constraints, ref<Expr> _expr)
	: constraints(_constraints), expr(_expr) { }

	Query(ref<Expr> _expr)
	: constraints(dummyConstraints), expr(_expr) {}

	virtual ~Query() {}

	/// withExpr - Return a copy of the query with the given expression.
	Query withExpr(ref<Expr> _expr) const { return Query(constraints, _expr); }

	/// withFalse - Return a copy of the query with a false expression.
	Query withFalse() const { return Query(constraints, MK_CONST(0, Expr::Bool)); }

	/// negateExpr - Return a copy of the query with the expression negated.
	Query negateExpr() const { return withExpr(Expr::createIsZero(expr)); }

	void print(std::ostream& os) const;
	Expr::Hash hash(void) const;
	
private:
	static ConstraintManager dummyConstraints;
};
}

#endif
