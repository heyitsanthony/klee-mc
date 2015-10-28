#ifndef KLEE_XCHKSEARCHER_H
#define KLEE_XCHKSEARCHER_H

#include <unordered_map>
#include "../Core/Searcher.h"

namespace klee
{

class XChkSearcher : public Searcher
{
public:
	XChkSearcher(Searcher* in_base);
	virtual ~XChkSearcher();

	Searcher* createEmpty(void) const override {
		return new XChkSearcher(base->createEmpty());
	}

	ExecutionState* selectState(bool allowCompact) override;
	void update(ExecutionState *current, States s) override;
	void printName(std::ostream &os) const override{
		os << "XChkSearcher\n";
	}

	void updateHash(ExecutionState* s, unsigned hash=0);
protected:
	struct UnschedInfo {
		std::string	as_dump;
		unsigned	hash;
	};
private:
	void xchk(ExecutionState* s);
	typedef std::unordered_map<ExecutionState*, UnschedInfo>
		as_hashes_ty;
	ExecutionState	*last_selected;
	as_hashes_ty	as_hashes;
	Searcher	*base;
};
}


#endif
