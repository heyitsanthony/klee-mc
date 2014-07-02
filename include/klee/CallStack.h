#ifndef KLEE_CALLSTACK_H
#define KLEE_CALLSTACK_H

#include "klee/StackFrame.h"
#include <vector>

namespace klee
{
class KInstruction;
class Assignment;
class Cell;
class CallStack : public std::vector<StackFrame>
{
public:
	typedef std::vector<const KInstruction*> insstack_ty;
	void evaluate(const Assignment& a);
	const Cell& getTopCell(unsigned i) const;
	const Cell& getLocalCell(unsigned sfi, unsigned i) const;
	void writeLocalCell(unsigned i, const ref<Expr>& value);

	bool hasLocal(const KInstruction *target) const;
	ref<Expr> readLocal(const KInstruction* target) const
	{ return getTopCell(target->getDest()).value; }

	insstack_ty getKInstStack(void) const;

	/* returns number of variables nuked */
	unsigned clearTail(void);

	uint64_t hash(void) const;
private:
	void writeLocalCell(unsigned sfi, unsigned i, const ref<Expr>& value);
};
}
#endif
