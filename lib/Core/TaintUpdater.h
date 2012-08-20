#ifndef TAINTUPDATER_H
#define TAINTUPDATER_H

#include "klee/Constraints.h"
#include "../Expr/ShadowAlloc.h"
#include "../Core/Searcher.h"

namespace klee
{
class TaintUpdater : public Searcher
{
public:
	TaintUpdater(
		Searcher *_baseSearcher,
		const ConstraintManager& cm)
	: baseSearcher(_baseSearcher)
	, baseConstraints(cm)
	, lastState(NULL) {}

	virtual ~TaintUpdater() { delete baseSearcher; }

	virtual Searcher* createEmpty(void) const
	{ return new TaintUpdater(
		baseSearcher->createEmpty(),
		baseConstraints); }

	ExecutionState &selectState(bool allowCompact);

	void update(ExecutionState *current, States s)
	{ baseSearcher->update(current, s); }
	bool empty() const { return baseSearcher->empty(); }

	void printName(std::ostream &os) const {
		os << "<TaintUpdater>";
		baseSearcher->printName(os);
		os << "</TaintUpdater>\n";
	}

private:
	Searcher		*baseSearcher;
	ConstraintManager	baseConstraints;
	ExecutionState		*lastState;
	ShadowVal		lastTaint;
};
}

#endif