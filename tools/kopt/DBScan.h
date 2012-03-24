#ifndef DBSCAN_H
#define DBSCAN_H

#include <map>
#include <list>
#include "klee/util/Ref.h"

namespace klee
{
class Solver;
class RuleBuilder;
class ExprRule;
class Expr;
class Array;
class DBScan
{
public:
	DBScan();
	virtual ~DBScan();
	void punchout(Solver* s);

private:
	typedef std::map<ref<Expr>, std::list<const ExprRule*> >
		komap_ty;

	bool queryKnockout(const ExprRule* er, Solver *s);
	void loadKnockouts(komap_ty& ko_map);

	ref<Array>	arr;
	RuleBuilder	*rb;
};
};
#endif
