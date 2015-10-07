#ifndef KLEE_EXPRALLOC_H
#define KLEE_EXPRALLOC_H

#include "klee/ExprBuilder.h"

namespace klee
{
/// ExprBuilder - Base expression builder class.
class ExprAlloc : public ExprBuilder
{
public:
	ExprAlloc() {}
	virtual ~ExprAlloc();

	virtual int compare(const Expr& lhs, const Expr& rhs)
	{ return lhs.compareDeep(rhs); }

	EXPR_BUILDER_DECL_ALL

	static unsigned long getNumConstants(void) { return constantCount; }
	virtual unsigned garbageCollect(void);

	void printName(std::ostream& os) const override;
protected:
	static unsigned long const_hit_c;
	static unsigned long const_miss_c;
	static unsigned long constantCount;
};
}

#endif
