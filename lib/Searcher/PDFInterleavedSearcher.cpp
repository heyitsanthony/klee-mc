#include <math.h>

#include "../Core/CoreStats.h"
#include "../Core/StatsTracker.h"
#include "klee/Internal/ADT/RNG.h"

#include "PDFInterleavedSearcher.h"

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
	for (auto s : _searchers)
		searchers.emplace_back(s);
	ticket_c = searchers.size();
}

void PDFInterleavedSearcher::update(ExecutionState *current, const States s)
{
	for (auto& ts : searchers)
		ts.searcher->update(current, s);
}

Searcher* PDFInterleavedSearcher::createEmpty(void) const
{
	std::vector<Searcher*>	new_s;

	for (const auto& s : searchers)
		new_s.push_back(s.searcher->createEmpty());

	return new PDFInterleavedSearcher(new_s);
}


void PDFInterleavedSearcher::setBaseTickets(unsigned idx, unsigned tickets)
{
	TicketSearcher *ts;

	assert (idx < searchers.size());
	ts = &searchers[idx];
	ts->tickets_base = tickets;
	if (ts->tickets_base > ts->tickets) {
		ticket_c -= ts->tickets;
		ticket_c += tickets;
		ts->tickets = tickets;
	}
}

ExecutionState* PDFInterleavedSearcher::selectState(bool allowCompact)
{
	ExecutionState	*es;
	TicketSearcher	*ts;
	double		rand_val;
	int64_t		remaining_tickets;
	uint64_t	new_found_ins_count;

	new_found_ins_count = stats::uncoveredInstructions;
	new_found_ins_count += stats::coveredInstructions;

	ts = &searchers[cur_searcher_idx];
	if (new_found_ins_count > last_uncov_ins) {
		int		payout;
		uint64_t	budget;

		budget = ts->tickets;
		payout =  ceil((1.0-budget/(double)ticket_c)*searchers.size());

		ticket_c += payout;
		ts->tickets = payout + budget;

		last_uncov_ins = new_found_ins_count;
		selects_since_new_ins = 0;
	} else if (last_ins != stats::instructions) {
		uint64_t	budget;
		int		penalty;

		selects_since_new_ins++;
		budget = ts->tickets;

		penalty = selects_since_new_ins;
		penalty = ceil(penalty*(budget/((double)ticket_c)))+1;
		if ((((int)budget) - penalty) < (int)ts->tickets_base)
			penalty = budget - ts->tickets_base;

		ts->tickets = budget - penalty;
		ticket_c -= penalty;
	}
	last_ins = stats::instructions;

	cur_searcher_idx = searchers.size() - 1;

	rand_val = theRNG.getDoubleL();
	remaining_tickets = ticket_c*rand_val;
	for (unsigned k = 0; k < searchers.size(); k++) {
		remaining_tickets -= searchers[k].tickets;
		if (remaining_tickets < 0) {
			cur_searcher_idx = k;
			break;
		}
	}

	es = searchers[cur_searcher_idx].searcher->selectState(allowCompact);
	if (!es) {
	for (unsigned k = 0; k < searchers.size(); k++) {
		cur_searcher_idx++;
		cur_searcher_idx %= searchers.size();
		es = searchers[cur_searcher_idx].searcher->selectState(allowCompact);
		if (es) break;
	}
	}

	if (!es) {
		return nullptr;
	}

	std::cerr
		<< "PDF: CHOOSING: IDX=" << cur_searcher_idx
		<< ". ST=" << es
		<< ". TICKETS=" << searchers[cur_searcher_idx].tickets
		<< ". RANDVAL=" << rand_val
		<< ". NAME=";
	searchers[cur_searcher_idx].searcher->printName(std::cerr);
	std::cerr << '\n';
	return es;
}
