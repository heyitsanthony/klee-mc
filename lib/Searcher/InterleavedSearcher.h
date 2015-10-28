#ifndef INTERLEAVEDSEARCHER_H
#define INTERLEAVEDSEARCHER_H

#include "../Core/Searcher.h"

namespace klee
{
class InterleavedSearcher : public Searcher
{
	typedef std::vector<Searcher*> searchers_ty;

	searchers_ty searchers;
	unsigned index;

public:
	explicit InterleavedSearcher(const searchers_ty &_searchers);
	virtual ~InterleavedSearcher();

	Searcher* createEmpty(void) const override;
	ExecutionState *selectState(bool allowCompact) override;
	void update(ExecutionState *current, const States s) override;

	void printName(std::ostream &os) const override {
		os << "<InterleavedSearcher> containing "
		<< searchers.size() << " searchers:\n";
		for (auto s : searchers) {
			s->printName(os);
			os << "</InterleavedSearcher>\n";
		}
	}
};
}

#endif
