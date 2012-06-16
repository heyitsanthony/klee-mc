#ifndef STPSOLVERIMPL_H
#define STPSOLVERIMPL_H

#include "klee/Solver.h"
#include "SolverImpl.h"
#include "STPBuilder.h"

namespace klee
{
class ExprHandle;

class STPSolverImpl : public SolverImpl {
private:
  /// The solver we are part of, for access to public information.
  STPSolver *solver;
  bool doForkedComputeInitialValues(
  	Assignment& a,
	ExprHandle& stp_e,
	bool& hasSolution);
protected:
  VC vc;
  STPBuilder *builder;
  double timeout;
  bool useForkedSTP;
  void setupVCQuery(const Query& query, ExprHandle& stp_e, std::ostream& os);
  void printDebugQueries(
	std::ostream& os,
	double t_check,
	const Assignment& a,
	bool hasSolution) const;
public:
  STPSolverImpl(STPSolver *_solver, bool _useForkedSTP);
  ~STPSolverImpl();

  void setTimeout(double _timeout) { timeout = _timeout; }

  bool computeSat(const Query&);
  bool computeInitialValues(const Query&, Assignment& a);
  void printName(int level = 0) const {
    klee_message("%*s" "STPSolverImpl", 2*level, "");
  }
};

class ServerSTPSolverImpl : public STPSolverImpl
{
  sockaddr_in_opt server;

public:
  ServerSTPSolverImpl(
  	STPSolver *_solver, bool _useForkedSTP, sockaddr_in_opt _server) :
    STPSolverImpl(_solver, _useForkedSTP), server(_server) { }

  bool computeInitialValues(const Query&, Assignment& );
  void printName(int level = 0) const {
    klee_message("%*s" "ServerSTPSolverImpl", 2*level, "");
  }

private:
  bool talkToServer(
  	double timeout, const char* query, unsigned long qlen,
	Assignment& a,
	bool &hasSolution);
};

}

#endif
