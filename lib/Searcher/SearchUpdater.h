#ifndef UPDATER_H
#define UPDATER_H

#include "klee/Constraints.h"
#include "../Core/Searcher.h"

namespace klee
{
class UpdateAction
{
public:
	virtual UpdateAction* copy(void) const = 0;
	virtual void selectUpdate(ExecutionState* es) = 0;
	virtual ~UpdateAction() {}
protected:
	UpdateAction() {}
private:
};

class SearchUpdater : public Searcher
{
public:
	SearchUpdater(Searcher *_base, UpdateAction* ua)
	: base(_base)
	, action(ua)
	, lastState(NULL) {}

	virtual ~SearchUpdater() { delete base; delete action; }

	virtual Searcher* createEmpty(void) const
	{ return new SearchUpdater(base->createEmpty(), action); }

	ExecutionState* selectState(bool allowCompact)
	{
		ExecutionState	*new_es = base->selectState(allowCompact);
		if (new_es != lastState)  {
			action->selectUpdate(new_es);
			lastState = new_es;
		}
		return new_es;
	}

	void update(ExecutionState *current, States s)
	{ base->update(current, s); }
	bool empty() const { return base->empty(); }

	void printName(std::ostream &os) const {
		os << "<SearchUpdater>";
		base->printName(os);
		os << "</SearchUpdater>\n";
	}

private:
	Searcher	*base;
	UpdateAction	*action;
	ExecutionState	*lastState;
};
}

#endif
