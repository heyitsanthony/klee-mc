#ifndef RANDOMVALUE_H
#define RANDOMVALUE_H

#include "klee/Expr.h"
#include <map>
#include <vector>

namespace klee
{
class Solver;
class Query;
class RandomValue;

class RandomValue
{
public:
	virtual ~RandomValue(void) {}
	static bool get(Solver* s, const Query& q, ref<ConstantExpr>& result);
	bool getValue(Solver* s, const Query& q, ref<ConstantExpr>& result);
protected:
	RandomValue(Expr::Hash eh) : query_hash(eh) {}


	ref<ConstantExpr> extend(Solver* s, const Query& q);
	ref<ConstantExpr> tryExtend(Solver* s, const Query& q);
private:
	typedef std::map<Expr::Hash, RandomValue*> rvmap_ty;
	static rvmap_ty rvmap;
	Expr::Hash			query_hash;
	ref<ConstantExpr>		min_res, max_res;
	std::vector<ref<ConstantExpr> >	seen;
};
}

#endif
