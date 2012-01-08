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
		Guest* gs,
		const char	*funcname,
		const char	*dirname,
		unsigned	test_num);

	klee::KTestStream* allocKTest(void) const;

	void save(const char* fname) const;
	virtual ~UCState();

protected:
	UCState(Guest* gs,
		const char	*funcname,
		const char	*dirname,
		unsigned	test_num);

private:
	void setupRegValues(klee::KTestStream* kts_uc);

	template <typename UCTabEnt>
	void loadUCBuffers(Guest* gs, klee::KTestStream* kts_uc);

	Guest		*gs;
	const char	*funcname;
	const char	*dirname;
	unsigned	test_num;
	bool		ok;

	typedef std::map<std::string, UCBuf*> ucbuf_map_ty;
	std::set<uint64_t>	uc_pages;
	ucbuf_map_ty		ucbufs;
};

#endif
