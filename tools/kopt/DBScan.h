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
	DBScan(Solver*);
	virtual ~DBScan();
	void punchout(void);

private:
	typedef std::map<ref<Expr>, std::list<const ExprRule*> >
		komap_ty;

	bool isKnockoutValid(const ExprRule* er, Solver *s);
	void loadKnockoutRulesFromBuilder(komap_ty& ko_map);

	ref<Array>	arr;
	RuleBuilder	*rb;
	Solver		*s;
};
};
#endif
