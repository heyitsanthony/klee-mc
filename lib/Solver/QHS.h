#ifndef KLEE_SOLVER_QHS_H
#define KLEE_SOLVER_QHS_H

#include "klee/Expr.h"
#include "klee/Query.h"

namespace klee
{
class QHSEntry
{
public:
	QHSEntry(
		const Query& _q, Expr::Hash _qh,
		bool _isSAT = false, uint64_t v=~0)
	: q(_q), qh(_qh), isSAT(_isSAT), value(v) {}
	~QHSEntry() {}

	Query		q;
	Expr::Hash	qh;
	bool		isSAT;
	uint64_t	value;
};

class QHSStore
{
public:
	virtual ~QHSStore() {}
	virtual bool lookupSAT(const QHSEntry& qhs) = 0;
	virtual void saveSAT(const QHSEntry& qhs) = 0;
	virtual bool lookupValue(QHSEntry& qhs) { return false; }
	virtual void saveValue(const QHSEntry& qhs) { }
protected:
	QHSStore() {}
};

class QHSSink : public QHSStore
{
public:
	virtual ~QHSSink(void)
	{
		delete src;
		delete dst;
	}
	QHSSink(QHSStore* _src, QHSStore* _dst)
	: src(_src)
	, dst(_dst)
	{ assert (src && dst); }

	virtual bool lookupSAT(const QHSEntry& qhs);
	virtual void saveSAT(const QHSEntry& qhs);

	virtual bool lookupValue(QHSEntry& qhs);
	virtual void saveValue(const QHSEntry& qhs);
protected:
	QHSStore *src, *dst;
};

class QHSDir : public QHSStore
{
public:
	virtual ~QHSDir(void) {}
	static QHSDir* create(const std::string& dn);
	virtual bool lookupSAT(const QHSEntry& qe);
	virtual void saveSAT(const QHSEntry& qe);
protected:
	QHSDir(const std::string& _dirname) : dirname(_dirname) {}
private:
	void writeSAT(const QHSEntry& qe);
	std::string dirname;
};
}

#endif
