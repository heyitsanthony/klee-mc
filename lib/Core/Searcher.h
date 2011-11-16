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
#include <list>
// FIXME: Move out of header, use llvm streams.
#include <ostream>
#include <stdint.h>

namespace llvm {
  class BasicBlock;
  class Function;
  class Instruction;
}

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
			const std::set<ExecutionState*>& r,
			const std::set<ExecutionState*>& i,
			const std::set<ExecutionState*>& u)
		: addedStates(a), removedStates(r)
		, ignoreStates(i), unignoreStates(u)
		{}
		virtual ~States() {}

		const std::set<ExecutionState*>& getAdded(void) const
		{ return addedStates; }

		const std::set<ExecutionState*>& getRemoved(void) const
		{ return removedStates; }

		const std::set<ExecutionState*>& getIgnored(void) const
		{ return ignoreStates; }

		const std::set<ExecutionState*>& getUnignored(void) const
		{ return unignoreStates; }

		static const std::set<ExecutionState*> emptySet;
	private:
		const std::set<ExecutionState*> &addedStates;
		const std::set<ExecutionState*> &removedStates;
		const std::set<ExecutionState*> &ignoreStates;
		const std::set<ExecutionState*> &unignoreStates;
	};
	Searcher();
	virtual ~Searcher();

	virtual ExecutionState &selectState(bool allowCompact) = 0;
	virtual void update(ExecutionState *current, const States s) = 0;
	virtual bool empty() const = 0;

	// prints name of searcher as a klee_message()
	// TODO: could probably make prettier or more flexible
	virtual void printName(std::ostream &os) const {
		os << "<unnamed searcher>\n";
	}

	void addState(ExecutionState *es, ExecutionState *current = 0)
	{
		std::set<ExecutionState*> tmp;
		tmp.insert(es);
		update(	current,
			States(	tmp,
				States::emptySet,
				States::emptySet,
				States::emptySet));
	}

	void removeState(ExecutionState *es, ExecutionState *current = 0)
	{
		std::set<ExecutionState*> tmp;
		tmp.insert(es);
		update(	current,
			States(	States::emptySet,
				tmp,
				States::emptySet,
				States::emptySet));
	}
};
}

#endif
