#ifndef TOPLEVELBUILDER_H
#define TOPLEVELBUILDER_H

#include "klee/ExprBuilder.h"

namespace klee
{
class TopLevelBuilder : public ExprBuilder
{
public:
	TopLevelBuilder(ExprBuilder* top, ExprBuilder* recur,
		bool _owns_recur=true)
	: eb_top(top), eb_recur(recur)
	, in_builder(false), owns_recur(_owns_recur)
	{
		assert (top != recur);
	}

	virtual ~TopLevelBuilder()
	{ delete eb_top; if (owns_recur) delete eb_recur; }

	EXPR_BUILDER_DECL_ALL

	virtual void printName(std::ostream& os) const;
private:
	ExprBuilder	*eb_top;
	ExprBuilder	*eb_recur;
	bool		in_builder;
	bool		owns_recur;
};
}
#endif
