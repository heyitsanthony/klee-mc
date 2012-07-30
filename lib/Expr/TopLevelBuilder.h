#ifndef TOPLEVELBUILDER_H
#define TOPLEVELBUILDER_H

#include "klee/ExprBuilder.h"

namespace klee
{
class TopLevelBuilder : public ExprBuilder
{
public:
	TopLevelBuilder(ExprBuilder* top, ExprBuilder* recur)
	: eb_top(top), eb_recur(recur), in_builder(false)
	{ assert (top != recur); }

	virtual ~TopLevelBuilder() { delete eb_top; delete eb_recur; }

	EXPR_BUILDER_DECL_ALL
private:
	ExprBuilder	*eb_top;
	ExprBuilder	*eb_recur;
	bool		in_builder;
};
}
#endif
