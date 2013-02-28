#ifndef UCSTATE_H
#define UCSTATE_H

#include <map>
#include "guest.h"

namespace klee
{
class KTestStream;
}

class UCBuf;

class UCState
{
public:
	static UCState* init(
		Guest		*gs,
		const char	*funcname,
		klee::KTestStream	*kts);

	void save(const char* fname) const;
	virtual ~UCState();

protected:
	UCState(Guest* gs, const char *funcname, klee::KTestStream *kts);

private:
	void setupRegValues(klee::KTestStream* kts_uc);

	void loadUCBuffers(Guest* gs, klee::KTestStream* kts_uc);

	Guest		*gs;
	const char	*funcname;
	bool		ok;

	std::set<uint64_t>	uc_pages;

	typedef std::vector<UCBuf*> ucbufs_ty;
	ucbufs_ty		ucbufs;
};

#endif
