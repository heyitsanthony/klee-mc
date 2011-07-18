#ifndef STPSOLVERIMPL_H
#define STPSOLVERIMPL_H

#include "klee/Solver.h"
#include "klee/SolverImpl.h"
#include "STPBuilder.h"

namespace klee
{

class STPSolverImpl : public SolverImpl {
private:
  /// The solver we are part of, for access to public information.
  STPSolver *solver;
  bool doForkedComputeInitialValues(
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values,
	ExprHandle& stp_e,
	bool& hasSolution);
protected:
  VC vc;
  STPBuilder *builder;
  double timeout;
  bool useForkedSTP;

public:
  STPSolverImpl(STPSolver *_solver, bool _useForkedSTP);
  ~STPSolverImpl();

  char *getConstraintLog(const Query&);
  void setTimeout(double _timeout) { timeout = _timeout; }

  bool computeTruth(const Query&, bool &isValid);
  bool computeValue(const Query&, ref<Expr> &result);
  bool computeInitialValues(const Query&,
                            const std::vector<const Array*> &objects,
                            std::vector< std::vector<unsigned char> > &values,
                            bool &hasSolution);
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

  bool computeInitialValues(const Query&,
                            const std::vector<const Array*> &objects,
                            std::vector< std::vector<unsigned char> > &values,
                            bool &hasSolution);
  void printName(int level = 0) const {
    klee_message("%*s" "ServerSTPSolverImpl", 2*level, "");
  }

private:
  bool talkToServer(double timeout, const char* query, unsigned long qlen,
                    const std::vector<const Array*> &objects,
                    std::vector< std::vector<unsigned char> > &values,
                    bool &hasSolution);
};

}

#endif
