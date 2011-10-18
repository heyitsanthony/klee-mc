#include "static/Sugar.h"
#include "klee/Constraints.h"
#include "ValidatingSolver.h"
#include "klee/util/Assignment.h"
#include "SMTPrinter.h"

using namespace klee;

bool ValidatingSolver::computeSat(const Query& query)
{
	bool	isSatOracle, isSatSolver;

	isSatSolver = solver->impl->computeSat(query);
	if (solver->impl->failed()) goto failed;

	isSatOracle = oracle->impl->computeSat(query);
	if (oracle->impl->failed()) goto failed;

	assert(	isSatSolver == isSatOracle &&
		"computeSat mismatch");

	return isSatSolver;

failed:
	failQuery();
	return false;
}

void ValidatingSolver::failQuery(void)
{
	solver->impl->ackFail();
	oracle->impl->ackFail();
	SolverImpl::failQuery();
}

Solver::Validity ValidatingSolver::computeValidity(const Query &query)
{
	Solver::Validity	oracleValidity, solverValidity;

	solverValidity = solver->impl->computeValidity(query);
	if (solver->impl->failed()) goto failed;

	oracleValidity = oracle->impl->computeValidity(query);
	if (oracle->impl->failed()) goto failed;

	if (oracleValidity != solverValidity) {
		std::cerr
			<< "Validity mismatch: "
			<< "oracle = "
			<< Solver::getValidityStr(oracleValidity) << " vs "
			<< Solver::getValidityStr(solverValidity) << " = solver.\n";
		SMTPrinter::print(std::cerr, query);
	}

	assert ((oracleValidity == solverValidity) &&
		"bad solver:  mismatched validity");

	return solverValidity;

failed:
	failQuery();
	return Solver::Unknown;
}

ref<Expr> ValidatingSolver::computeValue(const Query& query)
{
	bool		isSat;
	ref<Expr>	ret;

	ret = solver->impl->computeValue(query);
	if (solver->impl->failed()) goto failed;

	// We don't want to compare, but just make sure this is a legal
	// solution.
	isSat = oracle->impl->computeSat(
		query.withExpr(EqExpr::create(query.expr, ret)));
	if (oracle->impl->failed()) goto failed;

	assert(isSat && "bad solver: solver says SAT, oracle says unSAT");
	return ret;

failed:
	failQuery();
	return ret;
}

bool ValidatingSolver::computeInitialValues(
	const Query& query, Assignment& a)
{
	bool	hasSolution;


	hasSolution = solver->impl->computeInitialValues(query, a);
	if (solver->impl->failed()) goto failed;

	if (hasSolution) {
		checkIVSolution(query, a);
	} else {
		bool	isSat;
		isSat = oracle->impl->computeSat(query);
		if (oracle->impl->failed()) goto failed;

		assert(	!isSat &&
			"bad solver: solver says unsat, oracle says sat");
	}

	return hasSolution;
failed:
	failQuery();
	return false;
}

// Assert the bindings as constraints, and verify that the
// conjunction of the actual constraints is satisfiable.
void ValidatingSolver::checkIVSolution(const Query& query, Assignment &a)
{
	std::vector< ref<Expr> >	bindings;
	bool				isSat;

	foreach (it, a.bindingsBegin(), a.bindingsEnd()) {
		const Array				*array = it->first;
		const std::vector<unsigned char>	&values(it->second);

		for (unsigned j=0; j < array->mallocKey.size; j++) {
			unsigned char v = values[j];

			bindings.push_back(
			EqExpr::create(
				ReadExpr::create(
					UpdateList(array, 0),
					ConstantExpr::alloc(j, Expr::Int32)),
				ConstantExpr::alloc(v, Expr::Int8)));
		}
	}

	ConstraintManager	tmp(bindings);
	ref<Expr>		constraints;

	constraints = Expr::createIsZero(query.expr);
	foreach (it, query.constraints.begin(), query.constraints.end())
		constraints = AndExpr::create(constraints, *it);

	isSat = oracle->impl->computeSat(Query(tmp, constraints));
	if (oracle->impl->failed()) {
		failQuery();
		return;
	}

	/* validity has no bearing on solution, so don't check it */
	/* however, we do expect the query set to be satisfiable... */
	if (!isSat) {
		std::cerr << "Solver says yes. Oracle says no. Query:\n";
		query.print(std::cerr);
	}

	assert(	isSat &&
		"bad solver: solver says sat, oracle says unsat!");
}
