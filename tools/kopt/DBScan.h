#ifndef DBSCAN_H
#define DBSCAN_H

#include <iostream>
#include <map>
#include <vector>
#include "klee/util/Ref.h"

namespace klee
{
class Solver;
class RuleBuilder;
class ExprRule;
class Expr;
class Array;
class KnockoutRule;
class KnockoutClass;
class DBScan
{
public:
	DBScan(Solver*);
	virtual ~DBScan();
	void punchout(std::ostream& os);
	void histo(void);
private:
	typedef std::pair<const KnockoutClass*, ExprRule*> newrule_ty;
	typedef std::map<
		ref<Expr> /* knock out expr */,
		KnockoutClass*
	> kcmap_ty;

	typedef std::vector<KnockoutRule*> krlist_ty;

	void loadKnockoutRulesFromBuilder();
	void addRule(const ExprRule* er);
	void saveRules(
		const std::string& fname,
		const std::vector<const ExprRule*>& ers);

	ref<Array>	arr;
	RuleBuilder	*rb;
	Solver		*s;

	kcmap_ty			kc_map;
	krlist_ty			kr_list;
	std::vector<const ExprRule*>	uninteresting;

};
};
#endif
