//===-- Solver.cpp --------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Solver.h"
#include "SolverImpl.h"
#include "klee/SolverStats.h"
#include "klee/Constraints.h"

#include "../Core/TimingSolver.h"
#include "STPBuilder.h"

#include "klee/Expr.h"
#include "klee/Internal/Support/Timer.h"
#include "klee/util/Assignment.h"
#include "klee/util/ExprUtil.h"

#include "static/Sugar.h"
#include "llvm/Support/CommandLine.h"

#include "ValidatingSolver.h"
#include "BoolectorSolver.h"
#include "Z3Solver.h"
#include "PoisonCache.h"
#include "DummySolver.h"

#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
#include <iostream>
#include <string>

#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

using namespace klee;
using namespace llvm;

bool UseFastCexSolver;

namespace {
  cl::opt<bool>
  DebugValidateSolver("debug-validate-solver",
		      cl::init(false));

  cl::opt<bool>
  UseForkedSTP("use-forked-stp", cl::desc("Run STP in forked process"));

  cl::opt<sockaddr_in_opt>
  STPServer("stp-server", cl::value_desc("host:port"));

  cl::opt<bool>
  UseSTPQueryPCLog("use-stp-query-pc-log",
                   cl::init(false));

  cl::opt<bool, true>
  ProxyUseFastCexSolver(
  	"use-fast-cex-solver",
  	cl::desc("Use the fast CEX solver, whatever that is."),
  	cl::location(UseFastCexSolver),
	cl::init(false));

  cl::opt<bool>
  UseFastRangeSolver(
	"use-fast-range-solver",
	cl::desc("Use the fast range solver."),
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
  UsePoisonCacheExpr("use-pcache-expr",
  	cl::init(false),
	cl::desc("Cache/Reject poisonous query hashes."));

  cl::opt<bool>
  UsePoisonCacheExprSHAStr("use-pcache-shastr",
  	cl::init(false),
	cl::desc("Cache/Reject poisonous query SHA'd strings."));

  cl::opt<bool>
  UsePoisonCacheRewritePtr("use-pcache-rewriteptr",
  	cl::init(false),
	cl::desc("Cache/Reject poisonous query with pointers rewritten."));

  cl::opt<bool>
  UseBoolector(
  	"use-boolector",
	cl::init(false),
	cl::desc("Use boolector solver"));

  cl::opt<bool>
  UseZ3("use-z3",
  	cl::init(false),
	cl::desc("Use z3 solver"));

  cl::opt<bool>
  UseIndependentSolver("use-independent-solver",
                       cl::init(true),
		       cl::desc("Use constraint independence"));

  cl::opt<bool>
  UseQueryPCLog("use-query-pc-log",
                cl::init(false));

}

static Solver* createChainWithTimedSolver(
	std::string queryPCLogPath,
	std::string stpQueryPCLogPath,
	TimedSolver* &timedSolver)
{
	Solver		*solver;

	if (UseBoolector)
		timedSolver = new BoolectorSolver();
	else if (UseZ3)
		timedSolver = new Z3Solver();
	else
		timedSolver = new STPSolver(UseForkedSTP, STPServer);
	solver = timedSolver;

	if (UseSTPQueryPCLog && stpQueryPCLogPath.size())
		solver = createPCLoggingSolver(solver, stpQueryPCLogPath);

	if (UsePoisonCacheExpr)
		solver = new Solver(
			new PoisonCache(solver, new PHExpr()));
	if (UsePoisonCacheExprSHAStr)
		solver = new Solver(
			new PoisonCache(solver, new PHExprStrSHA()));
	if (UsePoisonCacheRewritePtr)
		solver = new Solver(
			new PoisonCache(solver, new PHRewritePtr()));

	if (UseFastCexSolver) solver = createFastCexSolver(solver);
	if (UseFastRangeSolver) solver = createFastRangeSolver(solver);
	if (UseCexCache) solver = createCexCachingSolver(solver);
	if (UseCache) solver = createCachingSolver(solver);
	if (UseIndependentSolver) solver = createIndependentSolver(solver);

	if (DebugValidateSolver) {
		/* oracle is another QF_BV solver */
		if (UseBoolector || UseZ3)
			solver = createValidatingSolver(
				solver,
				new STPSolver(UseForkedSTP, STPServer));
		else
			solver = createValidatingSolver(solver, timedSolver);
	}

	if (UseQueryPCLog && queryPCLogPath.size())
		solver = createPCLoggingSolver(solver, queryPCLogPath);

	klee_message("BEGIN solver description");
	solver->printName();
	klee_message("END solver description");

	return solver;
}

Solver* Solver::createChain(
	std::string queryPCLogPath,
	std::string stpQueryPCLogPath)
{
	TimedSolver	*timedSolver;
	return createChainWithTimedSolver(
		queryPCLogPath,
		stpQueryPCLogPath,
		timedSolver);
}

TimingSolver* Solver::createTimerChain(
	double timeout,
	std::string queryPCLogPath,
	std::string stpQueryPCLogPath)
{
	Solver		*solver;
	TimedSolver	*timedSolver;
	TimingSolver	*ts;

	solver = createChainWithTimedSolver(
		queryPCLogPath, stpQueryPCLogPath, timedSolver);
	ts = new TimingSolver(solver, timedSolver);
	timedSolver->setTimeout(timeout);

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

Solver::~Solver() { delete impl; }
SolverImpl::~SolverImpl() { }

bool Solver::failed(void) const
{
	bool	failure = impl->failed();
	impl->ackFail();
	assert (impl->failed() == false);
	return failure;
}

ref<Expr> SolverImpl::computeValue(const Query& query)
{
	std::vector<const Array*> objects;
	std::vector< std::vector<unsigned char> > values;
	bool	hasSolution;

	// Find the object used in the expression, and compute an assignment
	// for them.
	findSymbolicObjects(query.expr, objects);
	hasSolution = computeInitialValues(query.withFalse(), objects, values);
	if (failed()) return NULL;

	if (!hasSolution) query.print(std::cerr);

	assert(hasSolution && "state has invalid constraint set");

	// Evaluate the expression with the computed assignment.
	Assignment a(objects, values);
	return a.evaluate(query.expr);
}

bool Solver::evaluate(const Query& query, Validity &result) {
  assert(query.expr->getWidth() == Expr::Bool && "Invalid expression type!");

  // Maintain invariants implementations expect.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(query.expr)) {
    result = CE->isTrue() ? True : False;
    return true;
  }

  result = impl->computeValidity(query);
  return (impl->failed() == false);
}

Solver::Validity SolverImpl::computeValidity(const Query& query)
{
	bool isSat, isNegSat;

	isSat = computeSat(query);
	if (failed()) return Solver::Unknown;

	isNegSat = computeSat(query.negateExpr());
	if (failed()) return Solver::Unknown;

	assert ((isNegSat || isSat) && "Inconsistent model");

	if (isNegSat && !isSat) return Solver::False;
	if (!isNegSat && isSat) return Solver::True;

	assert (isNegSat == isSat);
	return Solver::Unknown;
}

bool Solver::mustBeTrue(const Query& query, bool &result)
{
	Solver::Validity	validity;

	assert(	query.expr->getWidth() == Expr::Bool &&
		"Invalid expression type!");

	// Maintain invariants implementations expect.
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(query.expr)) {
		result = CE->isTrue() ? true : false;
		return true;
	}

	validity = impl->computeValidity(query);
	result = (validity == Solver::True);
	return (failed() == false);
}

bool Solver::mustBeFalse(const Query& query, bool &result) {
  return mustBeTrue(query.negateExpr(), result);
}

bool Solver::mayBeTrue(const Query& query, bool &result)
{
	bool res;
	if (!mustBeFalse(query, res))
		return false;
	result = !res;
	return true;
}

bool Solver::mayBeFalse(const Query& query, bool &result)
{
	bool res;
	if (!mustBeTrue(query, res))
		return false;
	result = !res;
	return true;
}

bool Solver::getValue(const Query& query, ref<ConstantExpr> &result)
{
  // Maintain invariants implementation expect.
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(query.expr)) {
    result = CE;
    return true;
  }

  // FIXME: Push ConstantExpr requirement down.
  ref<Expr> tmp;
  tmp = impl->computeValue(query);
  if (failed()) return false;

  result = cast<ConstantExpr>(tmp);
  return true;
}

bool Solver::getInitialValues(
  const Query& query,
  const std::vector<const Array*> &objects,
  std::vector< std::vector<unsigned char> > &values)
{
  bool hasSolution;

  // FIXME: Propogate this out.
  hasSolution = impl->computeInitialValues(query, objects, values);
  if (failed()) return false;
  assert (hasSolution == true && "SHOULD HAVE A SOLUTION");
  return hasSolution;
}

// FIXME: REFACTOR REFACTOR REFACTOR REFACTOR
std::pair< ref<Expr>, ref<Expr> > Solver::getRange(const Query& query)
{
  ref<Expr> e = query.expr;
  Expr::Width width = e->getWidth();
  uint64_t min, max;

  if (width==1) {
    Solver::Validity result;
    bool	eval_ok;
    eval_ok = evaluate(query, result);
    assert(eval_ok && "computeValidity failed");
    switch (result) {
    case Solver::True:	min = max = 1; break;
    case Solver::False:	min = max = 0; break;
    default:		min = 0, max = 1; break;
    }
  } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(e)) {
    min = max = CE->getZExtValue();
  } else {
    // binary search for # of useful bits
    uint64_t lo=0, hi=width, mid, bits=0;
    while (lo<hi) {
      mid = lo + (hi - lo)/2;
      bool res;
      bool success;
      
      success = mustBeTrue(
        query.withExpr(
          EqExpr::create(
	    LShrExpr::create(e,
              ConstantExpr::create(
                mid, width)),
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
    bool success;
    success = mayBeTrue(
      query.withExpr(
        EqExpr::create(e, ConstantExpr::create(0, width))),
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
        bool success;
	success = mayBeTrue(
	  query.withExpr(
	    UleExpr::create(e, ConstantExpr::create(mid, width))),
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
      bool success;
      success = mustBeTrue(
        query.withExpr(
	  UleExpr::create(e, ConstantExpr::create(mid, width))),
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

Solver *klee::createValidatingSolver(Solver *s, Solver *oracle) {
  return new Solver(new ValidatingSolver(s, oracle));
}

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

void Query::print(std::ostream& os) const
{
	os << "Constraints {\n";
	foreach (it, constraints.begin(), constraints.end()) {
		(*it)->print(os);
		os << std::endl;
	}
	os << "}\n";
	expr->print(os);
	os << std::endl;
}

void SolverImpl::failQuery(void)
{
	++stats::queriesFailed;
	has_failed = true;
}
