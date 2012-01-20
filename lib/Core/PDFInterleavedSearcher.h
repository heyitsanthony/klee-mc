#ifndef PDFINTERLEAVEDSEARCHER_H
#define PDFINTERLEAVEDSEARCHER_H

#include "static/Sugar.h"
#include "Searcher.h"

namespace klee
{
class PDFInterleavedSearcher : public Searcher
{
private:
	typedef std::pair<unsigned, Searcher*>	ticket_searcher_ty;
	typedef std::vector<ticket_searcher_ty>	searchers_ty;

	searchers_ty	searchers;
	unsigned	cur_searcher_idx;
	uint64_t	ticket_c;
	uint64_t	last_uncov_ins;
	uint64_t	last_ins;

public:
	explicit PDFInterleavedSearcher(const std::vector<Searcher*> &_searchers);
	virtual ~PDFInterleavedSearcher();

	virtual Searcher* createEmpty(void) const;

	ExecutionState &selectState(bool allowCompact);
	void update(ExecutionState *current, const States s);
	bool empty() const { return searchers[0].second->empty(); }
	void printName(std::ostream &os) const
	{
		os	<< "<PDFInterleavedSearcher> containing "
			<< searchers.size()
			<< " searchers:\n";
		foreach (it, searchers.begin(), searchers.end())
			(*it).second->printName(os);

		os << "</PDFInterleavedSearcher>\n";
	};
};
}

#endif
