#ifndef COMPLETESEARCHER_H
#define COMPLETESEARCHER_H

#include "../Core/Searcher.h"

namespace klee
{
class CompleteSearcher : public Searcher
{
public:
	CompleteSearcher(Searcher* incomplete, Searcher *complete)
		: incompl(incomplete)
		, comple(complete)
	{}

	Searcher* createEmpty(void) const override {
		return new CompleteSearcher(
			incompl->createEmpty(),
			comple->createEmpty());
	}

	ExecutionState *selectState(bool allowCompact) override {
		auto es = incompl->selectState(allowCompact);
		if (!es) es = comple->selectState(allowCompact);
		return es;
	}

	void update(ExecutionState *current, const States s) override {
		incompl->update(current, s);
		comple->update(current, s);
	}

	void printName(std::ostream &os) const override {
		os << "<CompleteSearcher>\n";
		incompl->printName(os);
		comple->printName(os);
		os << "</CompleteSearcher>\n";
	}
private:
	usearcher_t	incompl;
	usearcher_t	comple;
};
};

#endif
