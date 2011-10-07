/**
 * From 10/6/11 lunch discussion.
 *
 * We're doing a lot of point optimizations in the expression builder
 * to get rid of useless expressions (e.g. extract[0:0] (bvxor 0x10 x))
 * This raises a question: how often are we submitting tautologies that are
 * masked by symbolic variables?
 *
 * So, run programs, pick off all expressions, run through solver
 * checking for tautologies.
 *
 */
#include "TautologyChecker.h"
#include "SMTPrinter.h"

#include "klee/Constraints.h"
#include "static/Sugar.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;
using namespace klee;

namespace {
	cl::opt<bool>
	DumpTautologies(
		"dump-tautologies",
		cl::desc("Write tautologies found by checker to SMT files"),
		cl::init(false));
}

#define SPLIT_QUERY	\
	if (!in_solver) { in_solver = true; splitQuery(q); in_solver = false; }

TautologyChecker::TautologyChecker(Solver* s)
: SolverImplWrapper(s)
, in_solver(false)
, top_solver(NULL)
, split_query_c(0)
, split_fail_c(0)
, tautology_c(0)
{}

TautologyChecker::~TautologyChecker()
{
	std::cerr
		<< "Tautology Stats"
		<< "\nSplit Queries: " << split_query_c
		<< "\nSplit Failures: " << split_fail_c
		<< "\nTautologies: " << tautology_c
		<< '\n';
}

bool TautologyChecker::computeSat(const Query& q)
{
	SPLIT_QUERY
	return doComputeSat(q);
}

Solver::Validity TautologyChecker::computeValidity(const Query& q)
{
	SPLIT_QUERY
	return doComputeValidity(q);
}

ref<Expr> TautologyChecker::computeValue(const Query& q)
{
	SPLIT_QUERY
	return doComputeValue(q);
}

bool TautologyChecker::computeInitialValues(
	const Query& q,
	const std::vector<const Array*> &objs,
	std::vector< std::vector<unsigned char> > &vals)
{
	SPLIT_QUERY
	return doComputeInitialValues(q, objs, vals);
}

void TautologyChecker::splitQuery(const Query& q)
{
	assert (top_solver && "Top solver not set.");

	foreach (it, q.constraints.begin(), q.constraints.end())
		checkExpr(*it);

	if (dyn_cast<ConstantExpr>(q.expr) == NULL)
		checkExpr(q.expr);
}

void TautologyChecker::checkExpr(const ref<Expr>& e)
{
	ConstraintManager	dummyConstraints;
	Query			q(dummyConstraints, e);
	Solver::Validity	validity;

	split_query_c++;

	validity = top_solver->impl->computeValidity(q);
	if (top_solver->impl->failed()) {
		split_fail_c++;
		top_solver->impl->ackFail();
		return;
	}

	if (validity != Solver::Unknown) {
		if (DumpTautologies)
			SMTPrinter::dump(q, "tautology");
		tautology_c++;
	}
}