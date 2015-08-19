#include <algorithm>
#include "QHSFile.h"


using namespace klee;

QHSFile::HashFile::HashFile(MemFile* _mf)
: mf(_mf)
, hashes((const Expr::Hash*)mf->getBuf())
{
	std::cerr << "[QHSFile] MemFile has " << mf->getNumBytes() << "bytes\n";

	/* I wasn't doing this before. OOPS!! */
//	std::stable_sort(
//		hashes,
//		&hashes[mf->getNumBytes() / sizeof(Expr::Hash)]);
}

bool QHSFile::HashFile::hasHash(Expr::Hash h) const
{
	return std::binary_search(
		hashes,
		&hashes[mf->getNumBytes() / sizeof(Expr::Hash)],
		h);
}

QHSFile::HashFile* QHSFile::HashFile::create(const char* fname)
{
	MemFile	*mf = MemFile::create(fname);
	return (mf) ? new HashFile(mf) : nullptr;
}

QHSFile* QHSFile::create(
	const char* cache_fdir,
	const char* pending_fdir)
{ return new QHSFile(cache_fdir, pending_fdir); }


QHSFile::QHSFile(
	const char* cache_fdir,
	const char* _pending_fdir)
{
	char	path[256];

	snprintf(path, 256, "%s/sat.hcache", cache_fdir);
	hf_sat.reset(HashFile::create(path));

	snprintf(path, 256, "%s/unsat.hcache", cache_fdir);
	hf_unsat.reset(HashFile::create(path));

	snprintf(path, 256, "%s/poison.hcache", cache_fdir);
	hf_poison.reset(HashFile::create(path));

	snprintf(path, 256, "%s/sat.pending", _pending_fdir);
	pend_sat = PendingFile::create(path);
	
	snprintf(path, 256, "%s/unsat.pending", _pending_fdir);
	pend_unsat = PendingFile::create(path);

	snprintf(path, 256, "%s/poison.pending", _pending_fdir);
	pend_poison = PendingFile::create(path);

	snprintf(path, 256, "%s/value.pending", _pending_fdir);
	pend_value = PendingValueFile::create(path);

	assert (pend_sat && pend_unsat && pend_value && pend_poison);
}

bool QHSFile::lookupSAT(const QHSEntry& qe)
{
	switch (qe.qr) {
	case QHSEntry::ERR:
		return (hf_poison && hf_sat->hasHash(qe.qh)) ||
			pend_poison->hasHash(qe.qh);
	
	case QHSEntry::SAT:
		return (hf_sat && hf_sat->hasHash(qe.qh)) ||
			pend_sat->hasHash(qe.qh);

	case QHSEntry::UNSAT:
		return ((hf_unsat && hf_unsat->hasHash(qe.qh)) ||
		 pend_unsat->hasHash(qe.qh));
	}

	return false;
}

void QHSFile::saveSAT(const QHSEntry& qe)
{
	switch (qe.qr) {
	case QHSEntry::ERR: pend_poison->add(qe.qh); break;
	case QHSEntry::SAT: pend_sat->add(qe.qh); break;
	case QHSEntry::UNSAT: pend_unsat->add(qe.qh); break;
	}
}

bool QHSFile::lookupValue(QHSEntry& qhs)
{
	uint64_t	v;

	qhs.qr = QHSEntry::ERR;
	if (pend_value->hasHash(qhs.qh, v) == false)
		return false;

	qhs.qr = QHSEntry::SAT;
	qhs.value = v;
	return true;
}

void QHSFile::saveValue(const QHSEntry& qhs)
{
	assert (qhs.isSAT());
	pend_value->add(qhs.qh, qhs.value);
}

bool QHSFile::PendingFile::hasHash(Expr::Hash h) const
{ return sat.count(h) != 0; }

QHSFile::PendingFile* QHSFile::PendingFile::create(const char* fname)
{
	FILE	*f;
	f = fopen(fname, "ab+");
	if (f == NULL) return NULL;
	return new PendingFile(f);
}

QHSFile::PendingFile::~PendingFile() { fclose(f); }

void QHSFile::PendingFile::add(Expr::Hash h) 
{
	fwrite(&h, sizeof(Expr::Hash), 1, f);
	fflush(f);
}

QHSFile::PendingFile::PendingFile(FILE* _f)
: f(_f)
{
	do {
		Expr::Hash	h;
		if (fread(&h, sizeof(Expr::Hash), 1, f) != 1)
			break;
		sat.insert(h);
	} while(1);

	std::cerr << "[QHSPending] Loaded " << sat.size() << " entries\n";
}


QHSFile::PendingValueFile* QHSFile::PendingValueFile::create(const char* fn)
{
	FILE	*f = fopen(fn, "ab+");
	if (f == NULL) return NULL;
	return new QHSFile::PendingValueFile(f);
}

QHSFile::PendingValueFile::PendingValueFile(FILE* _f)
: f(_f)
{
	do {
		Expr::Hash	h;
		uint64_t	v;
		if (fread(&h, sizeof(Expr::Hash), 1, f) != 1)
			break;
		if (fread(&v, sizeof(v), 1, f) != 1)
			break;
		values.insert(std::make_pair(h, v));
	} while(1);

	std::cerr << "[QHSPendingV] Loaded " << values.size() << " entries\n";
}

QHSFile::PendingValueFile::~PendingValueFile() { fclose(f); }

bool QHSFile::PendingValueFile::hasHash(Expr::Hash h, uint64_t& found_v) const
{
	pvmap_ty::const_iterator	it(values.find(h));

	if (it == values.end()) return false;

	found_v = it->second;
	return true;
}

void QHSFile::PendingValueFile::add(Expr::Hash h, uint64_t v)
{
	if (values.insert(std::make_pair(h, v)).second == false)
		return;

	fwrite(&h, sizeof(h), 1, f);
	fwrite(&v, sizeof(v), 1, f);
	fflush(f);
}
