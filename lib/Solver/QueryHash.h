#ifndef QUERYHASH_H
#define QUERYHASH_H

namespace klee
{

class Query;

class QueryHash
{
public:
	QueryHash(const char* in_name) : name(in_name) {}
	virtual ~QueryHash() {}
	virtual unsigned hash(const Query& q) const = 0;
	const char* getName(void) const { return name; }
private:
	const char* name;
};

/* default class declaration-- hash declared in cpp file */
#define DECL_QHASH(x,y)					\
class QH##x : public QueryHash				\
{							\
public:							\
	QH##x() : QueryHash(y) {}			\
	virtual ~QH##x() {}				\
	virtual unsigned hash(const Query &q) const;	\
private: };


DECL_QHASH(ExprStrSHA, "strsha")
DECL_QHASH(Default, "default")
DECL_QHASH(RewritePtr, "rewriteptr")

}

#endif
