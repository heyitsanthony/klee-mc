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
	foreach (it, searchers.begin(), searchers.end())
		delete *it;
}

ExecutionState &InterleavedSearcher::selectState(bool allowCompact)
{
	Searcher *s = searchers[--index];
	if (index == 0) index = searchers.size();
	ExecutionState* es = &s->selectState(allowCompact);
	return *es;
}

void InterleavedSearcher::update(ExecutionState *current, const States s)
{
	foreach(it, searchers.begin(), searchers.end()) 
		(*it)->update(current, s);
}

Searcher* InterleavedSearcher::createEmpty(void) const
{
	searchers_ty	new_s;
	foreach (it, searchers.begin(), searchers.end())
		new_s.push_back((*it)->createEmpty());

	return new InterleavedSearcher(new_s);
}