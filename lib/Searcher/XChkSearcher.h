#ifndef KLEE_XCHKSEARCHER_H
#define KLEE_XCHKSEARCHER_H

#include <tr1/unordered_map>
#include "../Core/Searcher.h"

namespace klee
{

class XChkSearcher : public Searcher
{
public:
	ExecutionState &selectState(bool allowCompact);
	XChkSearcher(Searcher* in_base);
	virtual ~XChkSearcher();
	virtual Searcher* createEmpty(void) const
	{ return new XChkSearcher(base->createEmpty()); }


	void updateHash(ExecutionState* s, unsigned hash=0);
	void update(ExecutionState *current, States s);
	bool empty() const { return base->empty(); }
	void printName(std::ostream &os) const { os << "XChkSearcher\n"; }
protected:
	struct UnschedInfo {
		std::string	as_dump;
		unsigned	hash;
	};
private:
	void xchk(ExecutionState* s);
	typedef std::tr1::unordered_map<ExecutionState*, UnschedInfo>
		as_hashes_ty;
	ExecutionState	*last_selected;
	as_hashes_ty	as_hashes;
	Searcher	*base;
};
}


#endif
