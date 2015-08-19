#include <sys/types.h>
#include <sys/stat.h>
#include "klee/Internal/ADT/zfstream.h"

#include "SMTPrinter.h"
#include "QHS.h"
#include "HashSolver.h"

using namespace klee;

static const char* sat_dirs[] =
{
	[QHSEntry::ERR] = "poison",
	[QHSEntry::SAT] =  "sat",
	[QHSEntry::UNSAT] = "unsat"
};


bool QHSSink::lookupSAT(const QHSEntry& qhs)
{
	if (dst->lookupSAT(qhs)) return true;

	if (src->lookupSAT(qhs)) {
		dst->saveSAT(qhs);
		return true;
	}

	return false;
}

void QHSSink::saveSAT(const QHSEntry& qhs) { dst->saveSAT(qhs); }

bool QHSSink::lookupValue(QHSEntry& qhs)
{
	if (dst->lookupValue(qhs))
		return true;
	
	if (src->lookupValue(qhs) == false)
		return false;

	/* found in source, so save to sink destination */
	dst->saveValue(qhs);
	return true;
}

void QHSSink::saveValue(const QHSEntry& qhs) { dst->saveValue(qhs); }

QHSDir* QHSDir::create(const std::string& _dirname)
{
	mkdir(_dirname.c_str(), 0770);
	mkdir((_dirname + "/sat").c_str(), 0770);
	mkdir((_dirname + "/unsat").c_str(), 0770);
	mkdir((_dirname + "/poison").c_str(), 0770);
	return new QHSDir(_dirname);
}

bool QHSDir::lookupSAT(const QHSEntry& qhs)
{
	char		path[256];
	const char	*subdir;
	struct stat	s;
	bool		found;

	subdir = sat_dirs[qhs.qr];
	snprintf(path, 256, "%s/%s/%016lx", dirname.c_str(), subdir, qhs.qh);

	found = stat(path, &s) == 0;
	if (found && s.st_size == 0) {
		/* seen this twice; better save the whole thing */

		/* if we're sinking into a file cache, no need to save here */
		if (!HashSolver::isSink() && HashSolver::isWriteSAT())
			writeSAT(qhs);
	}

	return found;
}

void QHSDir::saveSAT(const QHSEntry& qhs)
{
	const char	*subdir;
	char		path[256];
	FILE		*f;

	subdir =  sat_dirs[qhs.qr];
	snprintf(path, 256, "%s/%s/%016lx", dirname.c_str(), subdir, qhs.qh);
	f = fopen(path, "w");
	if (f != NULL) fclose(f);
}

void QHSDir::writeSAT(const QHSEntry& qhs)
{
	const char	*subdir;
	char		path[256];
	std::unique_ptr<gzofstream> os;

	/* dump to corresponding SAT directory */
	subdir = sat_dirs[qhs.qr];
	snprintf(path, 256, "%s/%s/%016lx", dirname.c_str(), subdir, qhs.qh);

	os = std::make_unique<gzofstream>(path, std::ios::in|std::ios::binary);
	SMTPrinter::print(*os, qhs.q, true);
}
