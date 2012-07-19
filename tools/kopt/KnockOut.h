#ifndef KNOCKOUT_H
#define KNOCKOUT_H

#include "klee/Expr.h"
#include "klee/util/ExprVisitor.h"
#include "KnockoutRule.h"

#include <vector>

namespace klee
{
class KnockOut : public ExprVisitor
{
public:
	KnockOut(ref<Array> _arr = NULL, int _uri = -1)
	: ExprVisitor(false, true)
	, arr(_arr)
	, uniqReplIdx(_uri)
	{ use_hashcons = false; }
	virtual ~KnockOut() {}

	void setParams(ref<Array>& _arr, int _uri = -1)
	{ arr = _arr; uniqReplIdx = _uri; }

	ref<Expr> apply(const ref<Expr>& e);

	// NOTE: this mean that tag users must also skip reads
	virtual Action visitRead(const ReadExpr& re)
	{ return Action::skipChildren(); }

	virtual Action visitConstant(const ConstantExpr& ce);

	unsigned getArrOff(void) const { return arr_off; }

	std::vector<replvar_t>::const_iterator begin(void) const
	{ return replvars.begin(); }

	std::vector<replvar_t>::const_iterator end(void) const
	{ return replvars.end(); }
	unsigned getNumReplVars(void) const { return replvars.size(); }
private:
	unsigned	arr_off;
	ref<Array>	arr;
	int		uniqReplIdx;

	bool isIgnoreConst(const ConstantExpr& ce);

	std::vector<replvar_t>	replvars;
};
}

#endif
