#include "klee/ExecutionState.h"
#include "../Core/ExeStateManager.h"
#include "../Core/Executor.h"
#include "../Core/PTree.h"
#include "static/Sugar.h"

#include "klee/Statistics.h"
#include "klee/Internal/ADT/DiscretePDF.h"
#include "klee/Internal/ADT/RNG.h"
#include "klee/Internal/System/Time.h"

#include "RandomPathSearcher.h"

using namespace klee;
using namespace llvm;

namespace klee { extern RNG theRNG; }


RandomPathSearcher::RandomPathSearcher(Executor &_executor)
: executor(_executor) {}

ExecutionState &RandomPathSearcher::selectState(bool allowCompact)
{
	unsigned	flips=0, bits=0;
	PTreeNode	*n;
	
	n = executor.pathTree->root;
	while (n->data == NULL) {
		unsigned numEnabledChildren = 0, enabledIndex = 0;

		assert (!n->children.empty() && "Empty leaf node");

		if (n->children.size() == 1) {
			n = n->children[0];
			continue;
		}

		for (unsigned i = 0; i < n->children.size(); i++) {
			if (!	(n->sums[i][PTree::WeightAndNoCompact]
				&& (allowCompact || n->sums[i][PTree::WeightAnd])))
			{
				continue;
			}

			if (numEnabledChildren == 0) {
				enabledIndex = i;
				numEnabledChildren++;
			} else if (n->sums[i][PTree::WeightAnd])
				numEnabledChildren++;
		}

		if (!numEnabledChildren)
			n = NULL; // assert below
		else if (numEnabledChildren == 1)
			n = n->children[enabledIndex];
		else if (n->children.size() == 2) {
			assert(numEnabledChildren == 2);
			if (bits==0) {
				flips = theRNG.getInt32();
				bits = 32;
			}
			--bits;
			n = (flips&(1<<bits)) ? n->children[0] : n->children[1];
		} else {
			unsigned idx = theRNG.getInt32() % numEnabledChildren;
			for (unsigned i = 0; i < n->children.size(); i++) {
				if (!	(n->sums[i][PTree::WeightAndNoCompact]
					&& (	allowCompact ||
						n->sums[i][PTree::WeightAnd])))
					continue;

				if (!idx) {
					n = n->children[i];
					break;
				} else
					idx--;
			}
			assert(!idx);
		}

		assert(n && "RandomPathSearcher hit unexpected dead end");
	}

	return *n->data;
}

void RandomPathSearcher::update(ExecutionState *current, const States s)
{
	std::set<ExecutionState*>	inflight;

	foreach (it, s.getAdded().begin(), s.getAdded().end()) {
		ExecutionState *es = *it;
		if (es->ptreeNode->data != es) {
			/* Node is probably replacing another node which
			 * is in the remove list. Ignore it. */
			assert (s.getRemoved().count(es->ptreeNode->data));
			inflight.insert(es);
		}
		es->ptreeNode->update(PTree::WeightRunnable, true);
	}

	foreach (it, s.getRemoved().begin(), s.getRemoved().end()) {
		ExecutionState *es = *it;

		if (es->ptreeNode == NULL)
			continue;

		if (inflight.size() && inflight.find(es) != inflight.end())
			continue;

		if (es->ptreeNode->data != es)
			std::cerr << "GOD DAMN IT: ES=" << (void*)es << 
				". vs data=" << es->ptreeNode->data << '\n';

		assert(es->ptreeNode->data == es && "Rmv w/ bad pNode data");
		es->ptreeNode->update(PTree::WeightRunnable, false);
	}
}

bool RandomPathSearcher::empty(void) const
{ return executor.stateManager->empty(); }
