#include "static/Sugar.h"
#include "klee/Constraints.h"
#include "ValidatingSolver.h"

using namespace klee;

bool ValidatingSolver::computeTruth(
	const Query& query,
	bool &isValidSolver) 
{
	bool	isValidOracle;

	if (!solver->impl->computeTruth(query, isValidSolver))
		return false;
	if (!oracle->impl->computeTruth(query, isValidOracle))
		return false;

	assert(	isValidSolver == isValidOracle && 
		"invalid solver result (computeTruth)");

	return true;
}

bool ValidatingSolver::computeValidity(
	const Query		&query,
	Solver::Validity	&validity)
{
	Solver::Validity	oracleValidity;
	bool			ok;

	ok = solver->impl->computeValidity(query, validity);
	if (!ok) return false;

	ok = oracle->impl->computeValidity(query, oracleValidity);
	if (!ok) return false;

	if (oracleValidity < validity) {
		std::cerr 
			<< "Validity mismatch: " 
			<< "oracle = "
			<< Solver::getValidityStr(oracleValidity) << " vs "
			<< Solver::getValidityStr(validity) << " = solver.\n";
		query.print(std::cerr);
	}

	assert ((oracleValidity >= validity) && 
		"bad solver: validity promotion");

	return ok;
}

bool ValidatingSolver::computeValue(
	const Query& query,
	ref<Expr> &result)
{
	bool	ok;
	bool	isValid;

	ok = solver->impl->computeValue(query, result);
	if (!ok) return false;

	// We don't want to compare, but just make sure this is a legal
	// solution.
	ok = oracle->impl->computeTruth(
		query.withExpr(NeExpr::create(query.expr, result)),
		isValid);
	if (!ok) return false;

//	assert(isSAT && "bad solver: solver says SAT, oracle says unSAT");
	return true;
}

bool ValidatingSolver::computeInitialValues(
	const Query& query,
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values,
	bool &hasSolution)
{
	bool	init_values_ok;

	init_values_ok = solver->impl->computeInitialValues(
		query, objects, values, hasSolution);
	if (init_values_ok == false)
		return false;

	if (hasSolution) {
		checkIVSolution(query, objects, values);
	} else {
		bool	isValid;
		if (!oracle->impl->computeTruth(query, isValid))
			return false;
		assert(	!isValid && 
			"bad solver: solver says unsat, oracle says valid");
	}

	return true;
}

// Assert the bindings as constraints, and verify that the
// conjunction of the actual constraints is satisfiable.
void ValidatingSolver::checkIVSolution(
	const Query& query,
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values)
{
	std::vector< ref<Expr> >	bindings;
	bool				isValid;//, isSAT;

	for (unsigned i = 0; i != values.size(); ++i) {
		const Array *array = objects[i];
		for (unsigned j=0; j<array->mallocKey.size; j++) {
			unsigned char value = values[i][j];
			bindings.push_back(
			EqExpr::create(
			ReadExpr::create(
				UpdateList(array, 0),
				ConstantExpr::alloc(j, Expr::Int32)),
				ConstantExpr::alloc(value, Expr::Int8)));
		}
	}

	ConstraintManager	tmp(bindings);
	ref<Expr>		constraints;
	
	constraints = Expr::createIsZero(query.expr);
	foreach (it, query.constraints.begin(), query.constraints.end())
		constraints = AndExpr::create(constraints, *it);

	if (!oracle->impl->computeTruth(Query(tmp, constraints), isValid))
		return;

#if 0
	/* validity has no bearing on solution, so don't check it */
	/* however, we do expect the query set to be satisfiable... */
	if (!isSAT) {
		std::cerr << "Solver says yes. Oracle says no. Query:\n";
		query.print(std::cerr);
	}

	assert(	isSAT && 
		"bad solver: solver says sat, oracle says unsat!");
#endif
}
