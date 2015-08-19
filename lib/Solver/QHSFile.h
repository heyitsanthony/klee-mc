#ifndef QHSFILE_H
#define QHSFILE_H

#include "klee/Internal/ADT/MemFile.h"
#include <tr1/unordered_set>
#include <tr1/unordered_map>
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
		virtual ~HashFile() = default;
		bool hasHash(Expr::Hash h) const;
	private:
		HashFile(MemFile* _mf);
		std::unique_ptr<MemFile>	mf;
		const Expr::Hash 		*hashes;
	};

	class PendingFile 
	{
	public:
		static PendingFile* create(const char* fname);
		virtual ~PendingFile();
		bool hasHash(Expr::Hash h) const;
		void add(Expr::Hash h);
	private:
	typedef std::tr1::unordered_set<Expr::Hash> pendingset_ty;
		PendingFile(FILE* _f);
		FILE		*f;
		pendingset_ty	sat;
	};

	class PendingValueFile
	{
	public:
		static PendingValueFile* create(const char* fname);
		virtual ~PendingValueFile();
		bool hasHash(Expr::Hash h, uint64_t& found_v) const;
		void add(Expr::Hash h, uint64_t v);
	private:
		typedef std::tr1::unordered_map<Expr::Hash, uint64_t> pvmap_ty;
		PendingValueFile(FILE* _f);
		FILE		*f;
		pvmap_ty	values;
	};

public:
	virtual ~QHSFile(void) = default;
	static QHSFile* create(
		const char* cache_fdir,
		const char* pending_fdir);
	virtual bool lookupSAT(const QHSEntry& qe);
	virtual void saveSAT(const QHSEntry& qe);

	virtual bool lookupValue(QHSEntry& qhs);
	virtual void saveValue(const QHSEntry& qhs);
protected:
	QHSFile(
		const char* cache_fdir,
		const char* pending_fname);
private:
	std::unique_ptr<HashFile>	hf_sat;
	std::unique_ptr<HashFile> 	hf_unsat;
	std::unique_ptr<HashFile>	hf_poison;

	PendingFile		*pend_sat;
	PendingFile		*pend_unsat;
	PendingFile		*pend_poison;
	PendingValueFile	*pend_value;

	std::vector<Expr::Hash>	queued_hashes;
};
}

#endif
