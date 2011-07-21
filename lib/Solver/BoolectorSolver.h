#ifndef BOOLECTORSOLVERIMPL_H
#define BOOLECTORSOLVERIMPL_H

#include "klee/Solver.h"
#include "SolverImpl.h"
#include <list>
#include <set>

struct BtorExp;
struct Btor;

namespace klee
{

class BoolectorSolver : public TimedSolver /* XXX: timing lipservice */
{
public:
	BoolectorSolver(void);
	virtual ~BoolectorSolver(void);
};


class BoolectorSolverImpl : public SolverImpl
{
public:
	BoolectorSolverImpl();
	~BoolectorSolverImpl();

	virtual bool computeSat(const Query&);
	virtual bool computeInitialValues(
		const Query&,
		const std::vector<const Array*> &objects,
		std::vector< std::vector<unsigned char> > &values);

	virtual void printName(int level = 0) const
	{
		klee_message("%*s" "BoolectorSolverImpl", 2*level, "");
	}

private:
	BtorExp* klee2btor(const ref<Expr>& klee_expr);
	void freeBtorExps(void);
	void assumeConstraints(const Query& q);

	BtorExp* getArrayForUpdate(const Array *root, const UpdateNode *un);
	BtorExp* getInitialArray(const Array *root);
	BtorExp* buildArray(
		const char *name, unsigned indexWidth, unsigned valueWidth);
	BtorExp* getOnes(unsigned w);
	BtorExp* getZeros(unsigned w);
	uint8_t getArrayValue(const Array *root, unsigned index);
	bool isSatisfiable(const Query& q);

	Btor				*btor;

	/* used to free expressions */
	std::set<BtorExp*>		exp_set;
	/* lists of array btorexps */
	std::list<void**>		array_list;
};

}

#endif
