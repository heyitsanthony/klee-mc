//===-- Searcher.h ----------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_SEARCHER_H
#define KLEE_SEARCHER_H

#include <vector>
#include <set>
#include <map>
#include <ostream>
#include <stdint.h>
#include <memory>

namespace klee
{
class ExecutionState;
class Executor;

class Searcher
{
public:
	class States
	{
	public:
		States(	const std::set<ExecutionState*>& a,
			const std::set<ExecutionState*>& r)
		: addedStates(a), removedStates(r)
		{}

		States(const std::set<ExecutionState*>& a);

		virtual ~States() {}

		const std::set<ExecutionState*>& getAdded(void) const
		{ return addedStates; }

		const std::set<ExecutionState*>& getRemoved(void) const
		{ return removedStates; }

		static const std::set<ExecutionState*> emptySet;
	private:
		const std::set<ExecutionState*> &addedStates;
		const std::set<ExecutionState*> &removedStates;
	};
	Searcher();
	virtual ~Searcher();

	virtual Searcher* createEmpty(void) const = 0;
	virtual ExecutionState &selectState(bool allowCompact) = 0;
	virtual void update(ExecutionState *current, const States s) = 0;
	virtual bool empty() const = 0;

	// prints name of searcher as a klee_message()
	// TODO: could probably make prettier or more flexible
	virtual void printName(std::ostream &os) const
	{ os << "<unnamed searcher>\n"; }

	void addState(ExecutionState *es, ExecutionState *current = 0)
	{
		std::set<ExecutionState*> tmp;
		tmp.insert(es);
		update(current, States(tmp,States::emptySet));
	}

	void removeState(ExecutionState *es, ExecutionState *current = 0)
	{
		std::set<ExecutionState*> tmp;
		tmp.insert(es);
		update(current, States(States::emptySet, tmp));
	}
};

typedef std::unique_ptr<Searcher> usearcher_t;
}

#endif
