#include "klee/ExecutionState.h"
#include "static/Sugar.h"

#include "InterleavedSearcher.h"

using namespace klee;

InterleavedSearcher::InterleavedSearcher(const std::vector<Searcher*> &_searchers)
: searchers(_searchers)
, index(1)
{}

InterleavedSearcher::~InterleavedSearcher()
{
	for (auto s : searchers) delete s;
}

ExecutionState *InterleavedSearcher::selectState(bool allowCompact)
{
	Searcher *s = searchers[--index];
	if (index == 0) index = searchers.size();
	auto es = s->selectState(allowCompact);
	for (unsigned k = 0; !es && k < searchers.size(); k++) {
		s = searchers[(index + k) % searchers.size()];
		es = s->selectState(allowCompact);
	}
	return es;
}

void InterleavedSearcher::update(ExecutionState *current, const States s)
{
	for (auto sr : searchers) sr->update(current, s);
}

Searcher* InterleavedSearcher::createEmpty(void) const
{
	searchers_ty	new_s;
	for (auto sr : searchers) new_s.push_back(sr->createEmpty());
	return new InterleavedSearcher(new_s);
}
