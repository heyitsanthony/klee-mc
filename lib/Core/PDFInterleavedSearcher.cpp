#include "klee/Internal/ADT/RNG.h"
#include "CoreStats.h"
#include "StatsTracker.h"
#include "PDFInterleavedSearcher.h"
#include <math.h>

namespace klee { extern RNG theRNG; }
using namespace klee;

PDFInterleavedSearcher::PDFInterleavedSearcher(
	const std::vector<Searcher*> &_searchers)
: cur_searcher_idx(0)
, ticket_c(0)
, last_uncov_ins(0)
, last_ins(0)
, selects_since_new_ins(0)
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
	ExecutionState	*es;
	double		rand_val;
	int64_t		remaining_tickets;
	uint64_t	new_found_ins_count;

	new_found_ins_count = stats::uncoveredInstructions;
	new_found_ins_count += stats::coveredInstructions;

	if (new_found_ins_count > last_uncov_ins) {
		int		payout;
		uint64_t	budget;

		budget = searchers[cur_searcher_idx].first;
		payout =  ceil((1.0-budget/(double)ticket_c)*searchers.size());
		ticket_c += payout;
		searchers[cur_searcher_idx].first = payout + budget;
		last_uncov_ins = new_found_ins_count;
		selects_since_new_ins = 0;
	} else if (last_ins != stats::instructions) {
		uint64_t	budget;
		int		penalty;

		selects_since_new_ins++;
		budget = searchers[cur_searcher_idx].first;

		penalty = selects_since_new_ins;
		penalty = ceil(penalty*(budget/((double)ticket_c)))+1;
		if (penalty >= (int)budget)
			penalty = budget - 1;


		searchers[cur_searcher_idx].first -= penalty;
		ticket_c -= penalty;
	}
	last_ins = stats::instructions;

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

	es = &searchers[cur_searcher_idx].second->selectState(allowCompact);
	std::cerr
		<< "PDF: CHOOSING: IDX=" << cur_searcher_idx
		<< ". ST=" << es
		<< ". TICKETS=" << searchers[cur_searcher_idx].first
		<< ". RANDVAL=" << rand_val
		<< ". NAME=";
	searchers[cur_searcher_idx].second->printName(std::cerr);
	std::cerr << '\n';
	return *es;
}
