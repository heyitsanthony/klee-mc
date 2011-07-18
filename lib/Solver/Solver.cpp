//===-- Solver.cpp --------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Solver.h"
#include "klee/SolverImpl.h"
#include "klee/SolverStats.h"
#include "klee/Constraints.h"

#include "../Core/TimingSolver.h"
#include "STPBuilder.h"

#include "klee/Expr.h"
#include "klee/Internal/Support/Timer.h"
#include "static/Sugar.h"
#include "llvm/Support/CommandLine.h"

#include "PoisonCache.h"

#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
#include <iostream>

#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

using namespace klee;
using namespace llvm;

namespace {
  cl::opt<bool>
  DebugValidateSolver("debug-validate-solver",
		      cl::init(false));

  cl::opt<bool>
  UseForkedSTP("use-forked-stp",
                 cl::desc("Run STP in forked process"));

  cl::opt<sockaddr_in_opt>
  STPServer("stp-server",
                 cl::value_desc("host:port"));

  cl::opt<bool>
  UseSTPQueryPCLog("use-stp-query-pc-log",
                   cl::init(false));

  cl::opt<bool>
  UseFastCexSolver("use-fast-cex-solver",
		   cl::init(false));

  cl::opt<bool>
  UseCexCache("use-cex-cache",
              cl::init(true),
	      cl::desc("Use counterexample caching"));

  cl::opt<bool>
  UseCache("use-cache",
	   cl::init(true),
	   cl::desc("Use validity caching"));

  cl::opt<bool>
  UsePoisonCache("use-poison-cache",
  	cl::init(false),
	cl::desc("Cache poisonous queries to disk. Fail on hit."));

  cl::opt<bool>
  UseIndependentSolver("use-independent-solver",
                       cl::init(true),
		       cl::desc("Use constraint independence"));

  cl::opt<bool>
  UseQueryPCLog("use-query-pc-log",
                cl::init(false));

}

TimingSolver* Solver::createChain(
	double timeout,
	std::string queryLogPath,
	std::string stpQueryLogPath,
	std::string queryPCLogPath,
	std::string stpQueryPCLogPath)
{
	STPSolver	*stpSolver;
	TimingSolver	*ts;

	stpSolver = new STPSolver(UseForkedSTP, STPServer);

	Solver *solver = stpSolver;

	if (UseSTPQueryPCLog) 
		solver = createPCLoggingSolver(solver, stpQueryPCLogPath);

	if (UsePoisonCache) solver = new Solver(new PoisonCache(solver));

	if (UseFastCexSolver) solver = createFastCexSolver(solver);
	if (UseCexCache) solver = createCexCachingSolver(solver);
	if (UseCache) solver = createCachingSolver(solver);

	if (UseIndependentSolver) solver = createIndependentSolver(solver);

	if (DebugValidateSolver)
		solver = createValidatingSolver(solver, stpSolver);

	if (UseQueryPCLog) solver = createPCLoggingSolver(solver, queryPCLogPath);

	klee_message("BEGIN solver description");
	solver->printName();
	klee_message("END solver description");

	ts = new TimingSolver(solver, stpSolver);
	stpSolver->setTimeout(timeout);

	return ts;
}

/***/

const char *Solver::validity_to_str(Validity v) {
  switch (v) {
  default:    return "Unknown";
  case True:  return "True";
  case False: return "False";
  }
}

Solver::~Solver() {
  delete impl;
}

SolverImpl::~SolverImpl() { }

bool Solver::evaluate(const Query& query, Validity &result) {
  assert(query.expr->getWidth() == Expr::Bool && "Invalid expression type!");

  // Maintain invariants implementations expect.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(query.expr)) {
    result = CE->isTrue() ? True : False;
    return true;
  }

  return impl->computeValidity(query, result);
}

bool SolverImpl::computeValidity(const Query& query, Solver::Validity &result) {
  bool isTrue, isFalse;
  if (!computeTruth(query, isTrue))
    return false;
  if (isTrue) {
    result = Solver::True;
  } else {
    if (!computeTruth(query.negateExpr(), isFalse))
      return false;
    result = isFalse ? Solver::False : Solver::Unknown;
  }
  return true;
}

bool Solver::mustBeTrue(const Query& query, bool &result) {
  assert(query.expr->getWidth() == Expr::Bool && "Invalid expression type!");

  // Maintain invariants implementations expect.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(query.expr)) {
    result = CE->isTrue() ? true : false;
    return true;
  }

  return impl->computeTruth(query, result);
}

bool Solver::mustBeFalse(const Query& query, bool &result) {
  return mustBeTrue(query.negateExpr(), result);
}

bool Solver::mayBeTrue(const Query& query, bool &result) {
  bool res;
  if (!mustBeFalse(query, res))
    return false;
  result = !res;
  return true;
}

bool Solver::mayBeFalse(const Query& query, bool &result) {
  bool res;
  if (!mustBeTrue(query, res))
    return false;
  result = !res;
  return true;
}

bool Solver::getValue(const Query& query, ref<ConstantExpr> &result) {
  // Maintain invariants implementation expect.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(query.expr)) {
    result = CE;
    return true;
  }

  // FIXME: Push ConstantExpr requirement down.
  ref<Expr> tmp;
  if (!impl->computeValue(query, tmp))
    return false;

  result = cast<ConstantExpr>(tmp);
  return true;
}

bool Solver::getInitialValues(
  const Query& query,
  const std::vector<const Array*> &objects,
  std::vector< std::vector<unsigned char> > &values)
{
  bool hasSolution;
  bool success;

  // FIXME: Propogate this out.
  success = impl->computeInitialValues(query, objects, values, hasSolution);
  if (!hasSolution)
    return false;

  return success;
}

// FIXME: REFACTOR REFACTOR REFACTOR REFACTOR
std::pair< ref<Expr>, ref<Expr> > Solver::getRange(const Query& query)
{
  ref<Expr> e = query.expr;
  Expr::Width width = e->getWidth();
  uint64_t min, max;

  if (width==1) {
    Solver::Validity result;
    if (!evaluate(query, result))
      assert(0 && "computeValidity failed");
    switch (result) {
    case Solver::True:
      min = max = 1; break;
    case Solver::False:
      min = max = 0; break;
    default:
      min = 0, max = 1; break;
    }
  } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(e)) {
    min = max = CE->getZExtValue();
  } else {
    // binary search for # of useful bits
    uint64_t lo=0, hi=width, mid, bits=0;
    while (lo<hi) {
      mid = lo + (hi - lo)/2;
      bool res;
      bool success =
        mustBeTrue(query.withExpr(
                     EqExpr::create(LShrExpr::create(e,
                                                     ConstantExpr::create(mid,
                                                                          width)),
                                    ConstantExpr::create(0, width))),
                   res);

      assert(success && "FIXME: Unhandled solver failure");
      (void) success;

      if (res) {
        hi = mid;
      } else {
        lo = mid+1;
      }

      bits = lo;
    }

    // could binary search for training zeros and offset
    // min max but unlikely to be very useful

    // check common case
    bool res = false;
    bool success =
      mayBeTrue(query.withExpr(EqExpr::create(e, ConstantExpr::create(0,
                                                                      width))),
                res);

    assert(success && "FIXME: Unhandled solver failure");
    (void) success;

    if (res) {
      min = 0;
    } else {
      // binary search for min
      lo=0, hi=bits64::maxValueOfNBits(bits);
      while (lo<hi) {
        mid = lo + (hi - lo)/2;
        bool res = false;
        bool success =
          mayBeTrue(query.withExpr(UleExpr::create(e,
                                                   ConstantExpr::create(mid,
                                                                        width))),
                    res);

        assert(success && "FIXME: Unhandled solver failure");
        (void) success;

        if (res) {
          hi = mid;
        } else {
          lo = mid+1;
        }
      }

      min = lo;
    }

    // binary search for max
    lo=min, hi=bits64::maxValueOfNBits(bits);
    while (lo<hi) {
      mid = lo + (hi - lo)/2;
      bool res;
      bool success =
        mustBeTrue(query.withExpr(UleExpr::create(e,
                                                  ConstantExpr::create(mid,
                                                                       width))),
                   res);

      assert(success && "FIXME: Unhandled solver failure");
      (void) success;

      if (res) {
        hi = mid;
      } else {
        lo = mid+1;
      }
    }

    max = lo;
  }

  return std::make_pair(ConstantExpr::create(min, width),
                        ConstantExpr::create(max, width));
}

void Solver::printName(int level) const {
  impl->printName(level);
}


/***/

class ValidatingSolver : public SolverImpl {
private:
  Solver *solver, *oracle;

public:
  ValidatingSolver(Solver *_solver, Solver *_oracle)
    : solver(_solver), oracle(_oracle) {}
  ~ValidatingSolver() { delete solver; }

  bool computeValidity(const Query&, Solver::Validity &result);
  bool computeTruth(const Query&, bool &isValid);
  bool computeValue(const Query&, ref<Expr> &result);
  bool computeInitialValues(const Query&,
                            const std::vector<const Array*> &objects,
                            std::vector< std::vector<unsigned char> > &values,
                            bool &hasSolution);

  void printName(int level = 0) const {
    klee_message("%*s" "ValidatingSolver containing:", 2*level, "");
    solver->printName(level + 1);
    oracle->printName(level + 1);
  }
};

bool ValidatingSolver::computeTruth(const Query& query,
                                    bool &isValid) {
  bool answer;

  if (!solver->impl->computeTruth(query, isValid))
    return false;
  if (!oracle->impl->computeTruth(query, answer))
    return false;

  if (isValid != answer)
    assert(0 && "invalid solver result (computeTruth)");

  return true;
}

bool ValidatingSolver::computeValidity(const Query& query,
                                       Solver::Validity &result) {
  Solver::Validity answer;

  if (!solver->impl->computeValidity(query, result))
    return false;
  if (!oracle->impl->computeValidity(query, answer))
    return false;

  if (result != answer)
    assert(0 && "invalid solver result (computeValidity)");

  return true;
}

bool ValidatingSolver::computeValue(const Query& query,
                                    ref<Expr> &result) {
  bool answer;

  if (!solver->impl->computeValue(query, result))
    return false;
  // We don't want to compare, but just make sure this is a legal
  // solution.
  if (!oracle->impl->computeTruth(query.withExpr(NeExpr::create(query.expr,
                                                                result)),
                                  answer))
    return false;

  if (answer)
    assert(0 && "invalid solver result (computeValue)");

  return true;
}

bool ValidatingSolver::computeInitialValues(
  const Query& query,
  const std::vector<const Array*> &objects,
  std::vector< std::vector<unsigned char> > &values,
  bool &hasSolution)
{
  bool answer;
  bool init_values_ok;

  init_values_ok = solver->impl->computeInitialValues(
  	query, objects, values, hasSolution);
  if (init_values_ok == false)
  	return false;

  if (hasSolution) {
    // Assert the bindings as constraints, and verify that the
    // conjunction of the actual constraints is satisfiable.
    std::vector< ref<Expr> > bindings;
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
    ConstraintManager tmp(bindings);
    ref<Expr> constraints = Expr::createIsZero(query.expr);
    foreach (it, query.constraints.begin(), query.constraints.end())
      constraints = AndExpr::create(constraints, *it);

    if (!oracle->impl->computeTruth(Query(tmp, constraints), answer))
      return false;
    if (!answer)
      assert(0 && "invalid solver result (computeInitialValues)");
  } else {
    if (!oracle->impl->computeTruth(query, answer))
      return false;
    if (!answer)
      assert(0 && "invalid solver result (computeInitialValues)");
  }

  return true;
}

Solver *klee::createValidatingSolver(Solver *s, Solver *oracle) {
  return new Solver(new ValidatingSolver(s, oracle));
}

/***/

class DummySolverImpl : public SolverImpl {
public:
  DummySolverImpl() {}

  bool computeValidity(const Query&, Solver::Validity &result) {
    ++stats::queries;
    // FIXME: We should have stats::queriesFail;
    return false;
  }
  bool computeTruth(const Query&, bool &isValid) {
    ++stats::queries;
    // FIXME: We should have stats::queriesFail;
    return false;
  }
  bool computeValue(const Query&, ref<Expr> &result) {
    ++stats::queries;
    ++stats::queryCounterexamples;
    return false;
  }
  bool computeInitialValues(const Query&,
                            const std::vector<const Array*> &objects,
                            std::vector< std::vector<unsigned char> > &values,
                            bool &hasSolution) {
    ++stats::queries;
    ++stats::queryCounterexamples;
    return false;
  }
  void printName(int level = 0) const {
    klee_message("%*s" "DummySolverImpl", 2*level, "");
  }
};

Solver *klee::createDummySolver() {
  return new Solver(new DummySolverImpl());
}

/***/
void SolverImpl::printDebugQueries(
	std::ostream& os,
	double t_check,
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values,
	bool hasSolution) const
{
	unsigned i=0;

	os	<< "STP CounterExample -- Has Solution: "
		<< hasSolution << " ("
		<< t_check/1000000.
		<< "s)\n";

	if (!hasSolution) goto flush;

	foreach (oi, objects.begin(), objects.end()) {
		const Array *obj = *oi;
		std::vector<unsigned char> &data = values[i++];
		os << " " << obj->name << " = [";
		for (unsigned j=0; j<obj->mallocKey.size; j++) {
			os << (int) data[j];
			if (j+1<obj->mallocKey.size) os << ",";
		}
		os << "]\n";
	}

flush:
	os.flush();
}

/***/

unsigned Query::hash(void) const
{
	unsigned	ret;

	ret = expr->hash();
	foreach (it, constraints.begin(), constraints.end()) {
		ref<Expr>	e = *it;
		ret ^= e->hash();
	}

	return ret;
}
