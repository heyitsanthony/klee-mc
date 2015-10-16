#ifndef FORK_HISTORY_H
#define FORK_HISTORY_H

#include <set>
#include <unordered_set>
#include <memory>
#include "klee/Expr.h"

namespace klee
{

class ExecutionState;
class ExprVisitor;
struct ForkInfo;

class ForkHistory
{
public:
	typedef std::set<std::pair<ref<Expr>, ref<Expr> > >	condxfer_t;
	typedef std::unordered_set<Expr::Hash>			succ_t;

	ForkHistory();
	virtual ~ForkHistory();

	void trackTransition(const ForkInfo& fi);

	const condxfer_t& conds(void) const { return condXfer; }
	bool hasSuccessor(const ExecutionState& st) const;
	bool hasSuccessor(const ref<Expr>& cond) const;

private:
	condxfer_t	condXfer;
	succ_t		hasSucc;
	std::unique_ptr<ExprVisitor> condFilter;
};
}
#endif
