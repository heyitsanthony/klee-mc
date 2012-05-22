#ifndef QHSFILE_H
#define QHSFILE_H

#include <tr1/unordered_set>
#include <stdio.h>

#include "HashSolver.h"

namespace klee
{
class MemFile;
class QHSFile : public QHSStore
{
private:
	class HashFile
	{
	public:
		static HashFile* create(const char* fname);
		virtual ~HashFile();
		bool hasHash(Expr::Hash h) const;
	private:
		HashFile(MemFile* _mf);
		MemFile		*mf;
		const Expr::Hash *hashes;
	};

	class PendingFile 
	{
	public:
		static PendingFile* create(const char* fname);
		virtual ~PendingFile();
		bool hasHash(Expr::Hash h) const { return sat.count(h) != 0; }
		void add(Expr::Hash h);
	private:
	typedef std::tr1::unordered_set<Expr::Hash> pendingset_ty;
		PendingFile(FILE* _f);
		FILE		*f;
		pendingset_ty	sat;
	};

public:
	virtual ~QHSFile(void);
	static QHSFile* create(
		const char* cache_fdir,
		const char* pending_fdir);
	virtual bool lookup(const QHSEntry& qe);
	virtual void saveSAT(const QHSEntry& qe);
protected:
	QHSFile(
		const char* cache_fdir,
		const char* pending_fname);
private:
	HashFile		*hf_sat;
	HashFile		*hf_unsat;

	PendingFile		*pend_sat;
	PendingFile		*pend_unsat;

	std::vector<Expr::Hash>	queued_hashes;
};
}

#endif
