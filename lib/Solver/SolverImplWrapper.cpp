#include "SolverImplWrapper.h"

using namespace klee;

SolverImplWrapper::~SolverImplWrapper() { delete wrappedSolver; }

bool SolverImplWrapper::doComputeSat(const Query& query)
{
	bool isSat;
	isSat = wrappedSolver->impl->computeSat(query);
	if (wrappedSolver->impl->failed())
		failQuery();
	return isSat;
}

ref<Expr> SolverImplWrapper::doComputeValue(const Query& query)
{
	ref<Expr> ret;
	ret = wrappedSolver->impl->computeValue(query);
	if (wrappedSolver->impl->failed())
		failQuery();
	return ret;
}

Solver::Validity SolverImplWrapper::doComputeValidity(const Query& query)
{
	Solver::Validity	ret;
	ret = wrappedSolver->impl->computeValidity(query);
	if (wrappedSolver->impl->failed())
		failQuery();
	return ret;
}

bool SolverImplWrapper::doComputeInitialValues(
	const Query& query,
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values)
{
	bool	hasSol;
	hasSol = wrappedSolver->impl->computeInitialValues(
		query, objects, values);
	if (wrappedSolver->impl->failed())
		failQuery();
	return hasSol;
}

void SolverImplWrapper::failQuery(void)
{
	wrappedSolver->impl->ackFail();
	SolverImpl::failQuery();
}
