#include "klee/Internal/ADT/RNG.h"
#include "CoreStats.h"
#include "StatsTracker.h"
#include "PDFInterleavedSearcher.h"

namespace klee { extern RNG theRNG; }
using namespace klee;

PDFInterleavedSearcher::PDFInterleavedSearcher(
	const std::vector<Searcher*> &_searchers)
: cur_searcher_idx(0)
, ticket_c(0)
, last_uncov_ins(0)
, last_ins(0)
{
	foreach (it, _searchers.begin(), _searchers.end())
		searchers.push_back(ticket_searcher_ty(1, *it));
	ticket_c = searchers.size();
}

PDFInterleavedSearcher::~PDFInterleavedSearcher()
{
	foreach (it, searchers.begin(), searchers.end())
		delete (*it).second;
}

void PDFInterleavedSearcher::update(ExecutionState *current, const States s)
{
	if (stats::uncoveredInstructions > last_uncov_ins) {
		ticket_c += searchers.size();
		searchers[cur_searcher_idx].first += searchers.size();
		last_uncov_ins = stats::uncoveredInstructions;
	} else if (
		last_ins != stats::instructions &&
		searchers[cur_searcher_idx].first > 1)
	{
		searchers[cur_searcher_idx].first--;
		ticket_c--;
	}
	last_ins = stats::instructions;

	foreach(it, searchers.begin(), searchers.end()) 
		(*it).second->update(current, s);
}

Searcher* PDFInterleavedSearcher::createEmpty(void) const
{
	std::vector<Searcher*>	new_s;

	foreach (it, searchers.begin(), searchers.end())
		new_s.push_back((*it).second->createEmpty());

	return new PDFInterleavedSearcher(new_s);
}

ExecutionState& PDFInterleavedSearcher::selectState(bool allowCompact)
{
	double		rand_val;
	int64_t		remaining_tickets;

	cur_searcher_idx = searchers.size() - 1;

	rand_val = theRNG.getDoubleL();
	remaining_tickets = ticket_c*rand_val;
	for (unsigned k = 0; k < searchers.size(); k++) {
		remaining_tickets -= searchers[k].first;
		if (remaining_tickets < 0) {
			cur_searcher_idx = k;
			break;
		}
	}

	std::cerr
		<< "PDF: CHOOSING: IDX=" << cur_searcher_idx
		<< ". TICKETS=" << searchers[cur_searcher_idx].first
		<< ". RANDVAL=" << rand_val
		<< ". NAME=";
	searchers[cur_searcher_idx].second->printName(std::cerr);
	return searchers[cur_searcher_idx].second->selectState(allowCompact);
}
