#ifndef KLEE_SOLVER_QHS_H
#define KLEE_SOLVER_QHS_H

#include "klee/Expr.h"
#include "klee/Query.h"

namespace klee
{
class QHSEntry
{
public:
	enum QueryResult {  ERR=0, SAT=1, UNSAT=2 };

	QHSEntry(
		const Query& _q, Expr::Hash _qh,
		QueryResult _qr, uint64_t v=~0)
	: q(_q), qh(_qh), qr(_qr), value(v) {}

	QHSEntry(
		const Query& _q, Expr::Hash _qh,
		bool _isSat=false, uint64_t v=~0)
	: q(_q), qh(_qh), qr((_isSat) ? SAT : UNSAT), value(v) {}

	~QHSEntry() = default;

	bool isSAT(void) const { return qr == SAT; }
	bool isUnsat(void) const { return qr == UNSAT; }
	bool isPoison(void) const { return qr == ERR; }

	Query		q;
	Expr::Hash	qh;
	QueryResult	qr;
	uint64_t	value;
};

class QHSStore
{
public:
	virtual ~QHSStore() = default;
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
	QHSSink(QHSStore* _src, QHSStore* _dst)
	: src(_src)
	, dst(_dst)
	{ assert (src && dst); }

	virtual ~QHSSink(void) = default;

	bool lookupSAT(const QHSEntry& qhs) override;
	void saveSAT(const QHSEntry& qhs) override;

	bool lookupValue(QHSEntry& qhs) override;
	void saveValue(const QHSEntry& qhs) override;
protected:
	std::unique_ptr<QHSStore>	src, dst;
};

class QHSDir : public QHSStore
{
public:
	virtual ~QHSDir(void) = default;
	static QHSDir* create(const std::string& dn);
	bool lookupSAT(const QHSEntry& qe) override;
	void saveSAT(const QHSEntry& qe) override;
protected:
	QHSDir(const std::string& _dirname) : dirname(_dirname) {}
private:
	void writeSAT(const QHSEntry& qe);
	std::string dirname;
};
}

#endif
