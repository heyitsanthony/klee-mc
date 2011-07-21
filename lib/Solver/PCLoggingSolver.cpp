//===-- PCLoggingSolver.cpp -----------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Solver.h"

#include "klee/Expr.h"
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

class PCLoggingSolver : public SolverImplWrapper
{
private:
  std::ofstream os;
  ExprPPrinter *printer;
  unsigned queryCount;
  double startTime;
  void dumpSolution(
  	const std::vector<const Array*> &objects,
        std::vector< std::vector<unsigned char> > &values,
  	bool hasSolution);

  void startQuery(const Query& query, const char *typeName,
                  const ref<Expr> *evalExprsBegin = 0,
                  const ref<Expr> *evalExprsEnd = 0,
                  const Array * const* evalArraysBegin = 0,
                  const Array * const* evalArraysEnd = 0) {
    Statistic *S = theStatisticManager->getStatisticByName("Instructions");
    uint64_t instructions = S ? S->getValue() : 0;
    os << "# Query " << queryCount++ << " -- "
       << "Type: " << typeName << ", "
       << "Instructions: " << instructions << "\n";
    printer->printQuery(os, query.constraints, query.expr,
                        evalExprsBegin, evalExprsEnd,
                        evalArraysBegin, evalArraysEnd);
    os.flush();

    startTime = estWallTime();
  }

  void finishQuery() {
    double delta = estWallTime() - startTime;
    os << "#   " << (failed() ? "FAIL" : "OK") << " -- "
       << "Elapsed: " << delta << "\n";
    os.flush();
  }

public:
  PCLoggingSolver(Solver *_solver, std::string path)
  : SolverImplWrapper(_solver),
    os(path.c_str(), std::ios::trunc),
    printer(ExprPPrinter::create(os)),
    queryCount(0) {
  }
  virtual ~PCLoggingSolver() { delete printer; }

  bool computeSat(const Query& query);
  Solver::Validity computeValidity(const Query& query);
  ref<Expr> computeValue(const Query& query);
  bool computeInitialValues(
	const Query& query,
        const std::vector<const Array*> &objects,
        std::vector< std::vector<unsigned char> > &values);

  void printName(int level = 0) const {
    klee_message("%*s" "PCLoggingSolver containing:", 2*level, "");
    wrappedSolver->printName(level + 1);
  }
};

bool PCLoggingSolver::computeSat(const Query& query)
{
	bool isSat;
	startQuery(query, "Truth");
	isSat = doComputeSat(query);
	finishQuery();
	if (!failed())
		os << "#   Is Sat: " << (isSat ? "true" : "false") << "\n";
	os << "\n";
	return isSat;
}

ref<Expr> PCLoggingSolver::computeValue(const Query& query)
{
	ref<Expr> ret;

	startQuery(query.withFalse(), "Value", &query.expr, &query.expr + 1);
	ret = doComputeValue(query);
	finishQuery();

	if (!failed()) os << "#   Result: " << ret << "\n";
	os << "\n";
	return ret;
}

Solver::Validity PCLoggingSolver::computeValidity(const Query& query)
{
	startQuery(query, "Validity");
	Solver::Validity ret = doComputeValidity(query);
	finishQuery();
	if (!failed()) os << "#   Validity: " << ret << "\n";
	os << "\n";
	return ret;
}

void PCLoggingSolver::dumpSolution(
	const std::vector<const Array*> &objects,
        std::vector< std::vector<unsigned char> > &values,
  	bool hasSolution)
{
	os << "#   Solvable: " << (hasSolution ? "true" : "false") << "\n";
	if (!hasSolution) return;

	std::vector< std::vector<unsigned char> >::iterator
	values_it = values.begin();
	foreach (i, objects.begin(), objects.end()) {
		const Array *array = *i;
		std::vector<unsigned char> &data = *values_it;

		os << "#     " << array->name << " = [";
		for (unsigned j = 0; j < array->mallocKey.size; j++) {
			os << (int) data[j];
			if (j+1 < array->mallocKey.size)
			os << ",";
		}
		os << "]\n";
		++values_it;
	}
}

bool PCLoggingSolver::computeInitialValues(
	const Query& query,
        const std::vector<const Array*> &objects,
        std::vector< std::vector<unsigned char> > &values)
{
	bool	hasSol;
	if (objects.empty()) {
		startQuery(query, "InitialValues", 0, 0);
	} else {
		startQuery(
			query, "InitialValues",
			0, 0,
			&objects[0], &objects[0] + objects.size());
	}

	hasSol = doComputeInitialValues(query, objects, values);
	finishQuery();

	if (!failed()) dumpSolution(objects, values, hasSol);
	os << "\n";

	return hasSol;
}

Solver *klee::createPCLoggingSolver(Solver *_solver, std::string path) {
  return new Solver(new PCLoggingSolver(_solver, path));
}
