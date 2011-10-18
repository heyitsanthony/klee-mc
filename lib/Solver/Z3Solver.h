#ifndef Z3SOLVERIMPL_H
#define Z3SOLVERIMPL_H

#include "klee/Solver.h"
#include "SolverImpl.h"
#include "z3.h"
#include <list>
#include <map>

namespace klee
{

class Z3Solver : public TimedSolver /* XXX: timing lipservice */
{
public:
	Z3Solver(void);
	virtual ~Z3Solver(void);
};

class Z3SolverImpl : public SolverImpl
{
public:
	Z3SolverImpl();
	~Z3SolverImpl();

	virtual bool computeSat(const Query&);
	virtual bool computeInitialValues(const Query&, Assignment& a);

	virtual void printName(int level = 0) const
	{ klee_message("%*s" "Z3SolverImpl", 2*level, ""); }

private:
	Z3_ast klee2z3(const ref<Expr>& klee_expr);
	void assumeConstraints(const Query& q);
	void addConstraint(const ref<Expr>& klee_expr);
	void cleanup(void);
	bool wasSat(Z3_lbool rc);
	Z3_sort getSort(unsigned w);
	Z3_ast boolify(Z3_ast ast);

	Z3_ast getArrayForUpdate(const Array *root, const UpdateNode *un);
	Z3_ast getInitialArray(const Array *root);
	uint8_t getArrayValue(Z3_model, const Array *root, unsigned index);

	Z3_config	z3_cfg;
	Z3_context	z3_ctx;	/* transient; lifespan = compute* call */

	std::map<unsigned , Z3_sort>	z3_sort_cache;
	std::list<void**>		z3_array_ptrs;
};

}

#endif
