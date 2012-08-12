//===-- Solver.cpp --------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
#include <llvm/Support/CommandLine.h>

#include "klee/Solver.h"
#include "SolverImpl.h"
#include "klee/SolverStats.h"
#include "klee/Constraints.h"
#include "klee/Expr.h"
#include "klee/Internal/Support/Timer.h"
#include "klee/util/Assignment.h"
#include "klee/util/ExprUtil.h"
#include "klee/ExprBuilder.h"

#include "static/Sugar.h"

#include "SMTPrinter.h"
#include "ValidatingSolver.h"
#include "PipeSolver.h"
#include "BoolectorSolver.h"
#include "Z3Solver.h"
#include "HashSolver.h"
#include "PoisonCache.h"
#include "DummySolver.h"
#include "TautologyChecker.h"
#include "HashComparison.h"
#include "RandomValue.h"

#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
#include <iostream>
#include <string>

#include <netdb.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

using namespace klee;
using namespace llvm;

bool	UseFastCexSolver;
bool	UseHashSolver;
double	MaxSTPTime;

#if 0
namespace llvm
{
namespace cl
{
template <>
class parser<sockaddr_in_opt> : public basic_parser<sockaddr_in_opt> {
public:
bool parse(Option&, const std::string&, const std::string&, sockaddr_in_opt &);
virtual const char *getValueName() const { return "sockaddr_in"; }
};
}
}

bool llvm::cl::parser<sockaddr_in_opt>::parse(
	llvm::cl::Option &O,
	const std::string& ArgName,
	const std::string &Arg,
	sockaddr_in_opt &Val)
{
  // find the separator
  std::string::size_type p = Arg.rfind(':');
  if (p == std::string::npos)
    return O.error("'" + Arg + "' not in format <host>:<port>");

  // read the port number
  unsigned short port;
  if (std::sscanf(Arg.c_str() + p + 1, "%hu", &port) < 1)
    return O.error("'" + Arg.substr(p + 1) + "' invalid port number");

  // resolve server name
  std::string host = Arg.substr(0, p);
  struct hostent* h = gethostbyname(host.c_str());
  if (!h)
    return O.error("cannot resolve '" + host + "' (" + hstrerror(h_errno) + ")");

  // prepare the return value
  Val.str = Arg;
  std::memset(&Val.sin, 0, sizeof(Val.sin));
  Val.sin.sin_family = AF_INET;
  Val.sin.sin_port = htons(port);
  Val.sin.sin_addr = *(struct in_addr*)h->h_addr;

  return false;
}
#endif
namespace {
  cl::opt<bool>
  DebugValidateSolver("debug-validate-solver", cl::init(false));

  cl::opt<bool>
  UseHashCmp("use-hash-cmp",
  	cl::desc("Compare two kinds of hashes on queries."), cl::init(false));

  cl::opt<bool>
  UseForkedSTP("use-forked-stp", cl::desc("Run STP in forked process"));

  cl::opt<sockaddr_in_opt>
  STPServer("stp-server", cl::value_desc("host:port"));

  cl::opt<bool>
  UseSTPQueryPCLog("use-stp-query-pc-log", cl::init(false));

  cl::opt<bool> UseSMTQueryLog("use-smt-log", cl::init(false));

  cl::opt<bool, true>
  ProxyUseFastCexSolver(
  	"use-fast-cex-solver",
  	cl::desc("Use the fast CEX solver, whatever that is."),
  	cl::location(UseFastCexSolver),
	cl::init(false));

  cl::opt<bool, true>
  ProxyUseHashSolver(
  	"use-hash-solver",
	cl::desc("Save hashes of queries w/results"),
  	cl::location(UseHashSolver),
  	cl::init(false));

  cl::opt<double,true>
  MaxSTPTimeProxy("max-stp-time",
	cl::desc("Maximum time for a single query (default=120s)"),
	cl::location(MaxSTPTime),
        cl::init(120.0));


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
  UseCache("use-cache", cl::init(true), cl::desc("Use validity caching"));

  cl::opt<bool>
  UseTautologyChecker(
  	"use-tautology-checker",
	cl::init(false),
	cl::desc("Run solver on bare formulas to find tautologies"));

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
  	"use-boolector", cl::init(false), cl::desc("Use boolector solver"));

  cl::opt<bool>
  UseZ3("use-z3", cl::init(false), cl::desc("Use z3 solver"));

  cl::opt<bool>
  UseB15("use-b15", cl::init(false), cl::desc("Use boolector-1.5 solver"));

  cl::opt<bool>
  UseCVC3("use-cvc3",
  	cl::init(false),
	cl::desc("Use CVC3 solver (broken)"));

  cl::opt<bool>
  UseYices("use-yices", cl::init(false), cl::desc("Use Yices solver"));

  cl::opt<bool>
  UsePipeSolver(
  	"pipe-solver",
	cl::init(false),
	cl::desc("Run solver through forked pipe."));

  cl::opt<bool>
  UseIndependentSolver("use-independent-solver",
                       cl::init(true),
		       cl::desc("Compute constraint independence"));

  cl::opt<bool>
  UseQueryPCLog("use-query-pc-log", cl::init(false));

  cl::opt<bool>
  XChkExprBuilder(
  	"xchk-expr-builder",
  	cl::desc("Cross check expression builder with oracle builder."),
	cl::init(false));

  cl::opt<bool>
  EquivExprBuilder(
  	"equiv-expr-builder",
	cl::desc("Use solver to find smaller, equivalent expressions."),
	cl::init(false));
}

namespace klee
{ extern Solver *createSMTLoggingSolver(Solver *_solver, std::string path); }

extern ExprBuilder *createXChkBuilder(
	Solver& solver,
	ExprBuilder* oracle, ExprBuilder* test);

extern ExprBuilder *createEquivBuilder(Solver& solver, ExprBuilder* test);


Solver* Solver::createChainWithTimedSolver(
	std::string queryPCLogPath,
	std::string stpQueryPCLogPath,
	TimedSolver* &timedSolver)
{
	Solver			*solver;
	TautologyChecker	*taut_checker = NULL;

	if (UsePipeSolver) {
		if (UseYices) {
			timedSolver = new PipeSolver(new PipeYices());
		} else if (UseCVC3) {
			timedSolver = new PipeSolver(new PipeCVC3());
		} else if (UseB15) {
			timedSolver = new PipeSolver(new PipeBoolector15());
		} else if (UseBoolector)
			timedSolver = new PipeSolver(new PipeBoolector());
		else if (UseZ3)
			timedSolver = new PipeSolver(new PipeZ3());
		else
			timedSolver = new PipeSolver(new PipeSTP());
	} else {
		assert (UseB15 == false);
		assert (UseCVC3 == false);
		assert (UseYices == false);
		if (UseBoolector)
			timedSolver = new BoolectorSolver();
		else if (UseZ3)
			timedSolver = new Z3Solver();
		else
			timedSolver = new STPSolver(UseForkedSTP, STPServer);
	}
	solver = timedSolver;

	if (UseSTPQueryPCLog && stpQueryPCLogPath.size())
		solver = createPCLoggingSolver(solver, stpQueryPCLogPath);
	else if (UseSMTQueryLog && stpQueryPCLogPath.size())
		solver = createSMTLoggingSolver(solver, stpQueryPCLogPath);

	if (UseTautologyChecker) {
		taut_checker = new TautologyChecker(solver);
		solver = new Solver(taut_checker);
	}

	if (UsePoisonCacheExpr)
		solver = new Solver(
			new PoisonCache(solver, new QHDefault()));
	if (UsePoisonCacheExprSHAStr)
		solver = new Solver(
			new PoisonCache(solver, new QHExprStrSHA()));
	if (UsePoisonCacheRewritePtr)
		solver = new Solver(
			new PoisonCache(solver, new QHRewritePtr()));

	if (UseHashSolver)
		solver = new Solver(
//			new HashSolver(solver, new QHRewritePtr()));
			new HashSolver(solver, new QHDefault()));

	if (UseHashCmp)
		solver = new Solver(
			new HashComparison(
				solver,
				new QHDefault(),
				new QHNormalizeArrays()));

	if (UseFastCexSolver) solver = createFastCexSolver(solver);
	if (UseFastRangeSolver) solver = createFastRangeSolver(solver);
	if (UseCexCache) solver = createCexCachingSolver(solver);
	if (UseCache) solver = createCachingSolver(solver);
	if (UseIndependentSolver) solver = createIndependentSolver(solver);

	if (DebugValidateSolver) {
		/* oracle is another QF_BV solver */
		if (UsePipeSolver || UseBoolector || UseZ3)
			solver = createValidatingSolver(
				solver,
				timedSolver);
				// new PipeSolver(new PipeSTP()));
				//new STPSolver(UseForkedSTP, STPServer));
		else
			solver = createValidatingSolver(solver, timedSolver);
	}

	if (UseQueryPCLog && queryPCLogPath.size())
		solver = createPCLoggingSolver(solver, queryPCLogPath);

	klee_message("BEGIN solver description");
	solver->printName();
	klee_message("END solver description");

	// tautology checker should submit partial queries to the top level;
	// caching will kick in and we don't have to waste time on
	// reoccurring queries
	if (taut_checker != NULL) {
		taut_checker->setTopLevelSolver(solver);
	}

	if (EquivExprBuilder) {
		Expr::setBuilder(
			createEquivBuilder(*solver, Expr::getBuilder()));
	}

	/* TODO: specify oracle builder */
	if (XChkExprBuilder) {
		ExprBuilder *xchkBuilder;
		xchkBuilder = createDefaultExprBuilder();
		xchkBuilder = createConstantFoldingExprBuilder(xchkBuilder);
		xchkBuilder = createSimplifyingExprBuilder(xchkBuilder);
		xchkBuilder = createXChkBuilder(
			*solver, xchkBuilder, Expr::getBuilder());

		Expr::setBuilder(xchkBuilder);
	}

	timedSolver->setTimeout(MaxSTPTime);

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

const char *Solver::validity_to_str(Validity v)
{
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
	bool		hasSolution;
	Assignment	a(query.expr);

	// Find the object used in the expression, and compute an assignment
	// for them.
	hasSolution = computeInitialValues(query.withFalse(), a);
	if (failed())
		return NULL;

	if (!hasSolution) query.print(std::cerr);

	assert(hasSolution && "state has invalid constraint set");

	// Evaluate the expression with the computed assignment.
	return a.evaluate(query.expr);
}

bool Solver::evaluate(const Query& query, Validity &result)
{
	assert(	query.expr->getWidth() == Expr::Bool &&
		"Invalid expression type!");

	in_solver = true;

	// Maintain invariants implementations expect.
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(query.expr)) {
		result = CE->isTrue() ? True : False;
		in_solver = false;
		return true;
	}

	result = impl->computeValidity(query);
	in_solver = false;
	return (failed() == false);
}

Solver::Validity SolverImpl::computeValidity(const Query& query)
{
	bool isSat, isNegSat;

	isSat = computeSat(query);
	if (failed()) return Solver::Unknown;

	isNegSat = computeSat(query.negateExpr());
	if (failed()) return Solver::Unknown;

	if (!isNegSat && !isSat) {
		SMTPrinter::dump(query, "incons");
		SMTPrinter::dump(query.negateExpr(), "incons.neg");
		klee_warning("Inconsistent Model. Killing Solver");
		failQuery();
		return Solver::Unknown;
	}

	if (isNegSat && !isSat) return Solver::False;
	if (!isNegSat && isSat) return Solver::True;

	assert (isNegSat == isSat);
	return Solver::Unknown;
}

bool Solver::mustBeFalse(const Query& query, bool &result)
{ return mustBeTrue(query.negateExpr(), result); }

bool Solver::mayBeFalse(const Query& query, bool &result)
{ return mayBeTrue(query.negateExpr(), result); }

bool Solver::mustBeTrue(const Query& query, bool &result)
{
	Solver::Validity	validity;

	assert(	query.expr->getWidth() == Expr::Bool &&
		"Invalid expression type!");

	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(query.expr)) {
		result = CE->isTrue();
		return true;
	}

	// Maintain invariants implementations expect.
	in_solver = true;
	validity = impl->computeValidity(query);
	result = (validity == Solver::True);
	in_solver = false;

	return (failed() == false);
}

bool Solver::mayBeTrue(const Query& query, bool &result)
{
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(query.expr)) {
		result = CE->isTrue();
		return true;
	}

	in_solver = true;
	result = impl->computeSat(query);
	in_solver = false;

	return (failed() == false);
}


bool Solver::getValueRandomized(const Query& query, ref<ConstantExpr>& result)
{ return RandomValue::get(this, query, result); }

bool Solver::getValue(const Query& query, ref<ConstantExpr> &result)
{ 
	if (query.expr->getKind() == Expr::Constant) {
		result = dyn_cast<ConstantExpr>(query.expr);
		return true;
	}

	return getValueRandomized(query, result);
}

bool Solver::getValueDirect(const Query& query, ref<ConstantExpr> &result)
{
	// Maintain invariants implementation expect.
	if (ConstantExpr *CE = dyn_cast<ConstantExpr>(query.expr)) {
		result = CE;
		return true;
	}

	// FIXME: Push ConstantExpr requirement down.
	ref<Expr> tmp;
	in_solver = true;
	tmp = impl->computeValue(query);
	in_solver = false;
	if (failed()) return false;

	if (tmp->getKind() != Expr::Constant)
		return false;
	result = cast<ConstantExpr>(tmp);

	return true;
}

bool Solver::getInitialValues(const Query& query, Assignment& a)
{
	bool hasSolution;

	// FIXME: Propagate this out.
	in_solver = true;
	hasSolution = impl->computeInitialValues(query, a);
	in_solver = false;
	if (failed()) return false;
	return hasSolution;
}

void Solver::printName(int level) const { impl->printName(level); }

Solver *klee::createValidatingSolver(Solver *s, Solver *oracle)
{ return new Solver(new ValidatingSolver(s, oracle)); }

Solver *klee::createDummySolver() { return new Solver(new DummySolverImpl()); }

void SolverImpl::failQuery(void)
{
	++stats::queriesFailed;
	has_failed = true;
}
