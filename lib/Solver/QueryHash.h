#ifndef QUERYHASH_H
#define QUERYHASH_H

#include "klee/Expr.h"

namespace klee
{

class Query;

class QueryHash
{
public:
	QueryHash(const char* in_name) : name(in_name) {}
	virtual ~QueryHash() {}
	virtual Expr::Hash hash(const Query& q) const = 0;
	const char* getName(void) const { return name; }
private:
	const char* name;
};

/* default class declaration-- hash declared in cpp file */
/* split into begin and end in case we want private vars */
#define DECL_QHASH_BEGIN(x,y)				\
class QH##x : public QueryHash				\
{							\
public:							\
	QH##x() : QueryHash(y) {}			\
	virtual ~QH##x() {}				\
	virtual Expr::Hash hash(const Query &q) const;	\
private: 

#define DECL_QHASH_END()	};
#define DECL_QHASH(x,y)		DECL_QHASH_BEGIN(x,y) DECL_QHASH_END()

DECL_QHASH(ExprStrSHA, "strsha")
DECL_QHASH(Default, "default")
DECL_QHASH(RewritePtr, "rewriteptr")
DECL_QHASH(NormalizeArrays, "normarrs")
	

}

#endif
