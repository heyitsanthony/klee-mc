#ifndef PDFINTERLEAVEDSEARCHER_H
#define PDFINTERLEAVEDSEARCHER_H

#include "static/Sugar.h"
#include "../Core/Searcher.h"

namespace klee
{
class PDFInterleavedSearcher : public Searcher
{
private:
class TicketSearcher
{
public:
	TicketSearcher(Searcher* s)
	: tickets_base(1)
	, tickets(tickets_base)
	, searcher(s) {}

	unsigned	tickets_base;
	unsigned	tickets;
	Searcher	*searcher;
};
	typedef std::vector<TicketSearcher>	searchers_ty;

	searchers_ty	searchers;
	unsigned	cur_searcher_idx;
	uint64_t	ticket_c;
	uint64_t	last_uncov_ins;
	uint64_t	last_ins;

	unsigned	selects_since_new_ins;

public:
	explicit PDFInterleavedSearcher(const std::vector<Searcher*> &_searchers);
	virtual ~PDFInterleavedSearcher();

	Searcher* createEmpty(void) const override;

	ExecutionState *selectState(bool allowCompact) override;
	void update(ExecutionState *current, const States s) override;
	void printName(std::ostream &os) const override {
		os << "<PDFInterleavedSearcher>\n";
		for (auto s : searchers) s.searcher->printName(os);
		os << "</PDFInterleavedSearcher>\n";
	}

	void setBaseTickets(unsigned idx, unsigned tickets);
};
}

#endif
