#ifndef KOCLASS_H
#define KOCLASS_H

#include <vector>

/* defines an equivalence class for a knockout object */
namespace klee
{
class ExprRule;
class KnockoutRule;

class KnockoutClass
{
public:
	typedef std::vector<const KnockoutRule*>	rules_ty;

	KnockoutClass(const KnockoutRule* _kr);
	virtual ~KnockoutClass() {}

	void addRule(const KnockoutRule* er);

	const KnockoutRule* front(void) const { return root_kr; }
	rules_ty::const_iterator begin(void) const { return rules.begin(); }
	rules_ty::const_iterator end(void) const { return rules.end() ; }
	unsigned size(void) const { return rules.size(); }

	ExprRule* createRule(Solver* s) const;
private:
	KnockoutClass() {}

	/* main knockout-rule */
	const KnockoutRule	*root_kr;
	/* list of additional knock-out rules */
	rules_ty		rules;

	typedef std::map<int, std::set<uint64_t> >	tagvals_ty;
	tagvals_ty	tagvals;

	exprtags_ty	tags;
};
}


#endif