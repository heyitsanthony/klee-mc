//===-- SMTLoggingSolver.cpp -----------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Solver.h"

#include "klee/Expr.h"
#include "SMTPrinter.h"
#include "SolverImplWrapper.h"
#include "klee/Statistics.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/Internal/Support/QueryLog.h"
#include "klee/Internal/System/Time.h"

#include "llvm/Support/CommandLine.h"
#include "static/Sugar.h"

#include <fstream>

using namespace klee;
using namespace klee::util;

namespace klee
{

class SMTLoggingSolver : public SolverImplWrapper
{
private:
	std::ofstream	os;
	unsigned	queryCount;
	double		startTime;

	void startQuery(const Query& query, const char *typeName);
	void finishQuery(void);
public:
	SMTLoggingSolver(Solver *_solver, std::string path)
	: SolverImplWrapper(_solver)
	, os(path.c_str()
	, std::ios::trunc)
	, queryCount(0) {}

	virtual ~SMTLoggingSolver() {}

	bool computeSat(const Query& query);
	Solver::Validity computeValidity(const Query& query);
	ref<Expr> computeValue(const Query& query);
	bool computeInitialValues(const Query& query, Assignment& a);

	void printName(int level = 0) const
	{
		klee_message("%*s" "SMTLoggingSolver containing:", 2*level, "");
		wrappedSolver->printName(level + 1);
	}
};

Solver* createSMTLoggingSolver(Solver *_solver, std::string path)
{
  return new Solver(new SMTLoggingSolver(_solver, path));
}
}

bool SMTLoggingSolver::computeSat(const Query& query)
{
	bool isSat;

	startQuery(query, "computeSAT");
	isSat = doComputeSat(query);
	finishQuery();
	return isSat;
}

ref<Expr> SMTLoggingSolver::computeValue(const Query& query)
{
	ref<Expr> ret;

	startQuery(query.withFalse(), "computeValue");
	ret = doComputeValue(query);
	finishQuery();

	return ret;
}

Solver::Validity SMTLoggingSolver::computeValidity(const Query& query)
{
	startQuery(query, "computeValidity");
	Solver::Validity ret = doComputeValidity(query);
	finishQuery();
	return ret;
}

bool SMTLoggingSolver::computeInitialValues(
	const Query& query, Assignment& a)
{
	bool	hasSol;

	startQuery(query, "InitialValues");
	hasSol = doComputeInitialValues(query, a);
	finishQuery();

	return hasSol;
}

void SMTLoggingSolver::startQuery(const Query& query, const char *typeName)
{
	Statistic	*S;
	uint64_t	instructions;

	S = theStatisticManager->getStatisticByName("Instructions");
	instructions = S ? S->getValue() : 0;
	os << "===# Query#" << queryCount++
		<< " Ins#" << instructions
		<< " Type=" << typeName
		<< " Hash=" << (void*)query.hash()
		<< "============\n";

	SMTPrinter::print(os, query);
	os.flush();

	startTime = estWallTime();
}

void SMTLoggingSolver::finishQuery(void)
{
	double delta = estWallTime() - startTime;
	os 	<< "========" << (failed() ? "FAIL" : "OK")
		<< " Elapsed: " << delta << "\n";
	os.flush();
}