#include "static/Sugar.h"
#include "klee/Constraints.h"
#include "ValidatingSolver.h"
#include "klee/util/Assignment.h"
#include "SMTPrinter.h"

using namespace klee;

#define CHK_ASSUMPTIONS					\
	ConstraintManager	cm;			\
	bool			okConstrs;		\
	assert (!oracle->impl->failed() && !solver->impl->failed()); \
	okConstrs = oracle->impl->computeSat(		\
		Query(cm, query.constraints.getConjunction()));	\
	if (oracle->impl->failed()) {				\
		oracle->impl->ackFail();			\
		okConstrs = true; } 				\
	assert (okConstrs && "Bad assumptions");		\
	assert (!oracle->impl->failed() && !solver->impl->failed());


#define TAG "[ValidatingSolver] "

bool ValidatingSolver::computeSat(const Query& query)
{
	bool	isSatOracle, isSatSolver;
	bool	okSolver, okOracle;

	CHK_ASSUMPTIONS;

	isSatSolver = solver->impl->computeSat(query);
	okSolver = !solver->impl->failed();
	if (!okSolver) std::cerr << TAG"solver failed\n";

	assert (!oracle->impl->failed());
	isSatOracle = oracle->impl->computeSat(query);
	okOracle = !oracle->impl->failed();
	if (!okOracle) std::cerr << TAG"oracle failed\n";

	if (!okOracle && !okSolver) goto failed;
	if (!okOracle) { oracle->impl->ackFail(); return isSatSolver; }
	if (!okSolver) { solver->impl->ackFail(); return isSatOracle; }

	if (isSatSolver != isSatOracle) SMTPrinter::dump(query, "computesat");
	assert(	isSatSolver == isSatOracle && "computeSat mismatch");

	return isSatSolver;

failed:
	std::cerr << TAG"full failure\n";
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

	CHK_ASSUMPTIONS;

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
		SMTPrinter::dump(query, "validity");
		std::cerr << "oracle:\n";
		oracle->printName();
		std::cerr << "test:\n";
		solver->printName();
		std::cerr << '\n';
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
	ref<Expr>	ret, ret2;

	CHK_ASSUMPTIONS;

	ret = solver->impl->computeValue(query);
	if (solver->impl->failed()) goto failed;

	// don't want to compare; just make sure this is a legal solution.
	isSat = oracle->impl->computeSat(query.withExpr(MK_EQ(query.expr, ret)));
	if (oracle->impl->failed()) goto failed;

#define TWO_WAY_VALUE_CHK
#ifdef TWO_WAY_VALUE_CHK
	ret2 = oracle->impl->computeValue(query);
	if (oracle->impl->failed()) {
		oracle->impl->ackFail();
		goto ok;
	}

	isSat = solver->impl->computeSat(query.withExpr(MK_EQ(query.expr, ret)));
	if (solver->impl->failed()) {
		isSat = true;
		solver->impl->ackFail();
	}
#endif
ok:
	if (!isSat) satMismatch(query);
	return ret;
failed:
	failQuery();
	return ret;
}

bool ValidatingSolver::computeInitialValues(
	const Query& query, Assignment& a)
{
	bool	hasSolution;

	CHK_ASSUMPTIONS;

	hasSolution = solver->impl->computeInitialValues(query, a);
	if (solver->impl->failed()) goto failed;

	if (hasSolution) {
		checkIVSolution(query, a);
	} else {
		bool	isSat;
		isSat = oracle->impl->computeSat(query);
		if (oracle->impl->failed()) goto failed;
		if (!isSat) satMismatch(query);
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

			bindings.push_back(MK_EQ(
				MK_READ(UpdateList(ARR2REF(array), 0),
					MK_CONST(j, Expr::Int32)),
				MK_CONST(v, Expr::Int8)));
		}
	}

	ConstraintManager	tmp(bindings);
	ref<Expr>		constraints;

	constraints = Expr::createIsZero(query.expr);
	foreach (it, query.constraints.begin(), query.constraints.end())
		constraints = MK_AND(constraints, *it);

	isSat = oracle->impl->computeSat(Query(tmp, constraints));
	if (oracle->impl->failed()) {
		failQuery();
		return;
	}

	/* validity has no bearing on solution, so don't check it */
	/* however, we do expect the query set to be satisfiable... */
	if (!isSat) satMismatch(query);
}

void ValidatingSolver::satMismatch(const Query& query)
{
	std::cerr << "Solver says yes. Oracle says no. Query "
		<< (void*)query.hash() << "\n";
	SMTPrinter::dump(query, "satmismatch");
	assert(false && "bad solver: solver says SAT, oracle says unSAT");
}
