#ifndef PIPESOLVER_H
#define PIPESOLVER_H

#include <sys/types.h>
#include "klee/Solver.h"
#include "SolverImpl.h"
#include "PipeFormat.h"
#include <ext/stdio_filebuf.h>
#include <list>
#include <set>

namespace klee
{
class PipeSolver : public TimedSolver /* XXX: timing lipservice */
{
public:
	PipeSolver(PipeFormat* fmt);
	virtual ~PipeSolver(void);
	virtual void setTimeout(double in_timeout);
};

class PipeSolverSession;

/* Pipes query to a SMT compatible printer. */
class PipeSolverImpl : public SolverImpl
{
public:
	PipeSolverImpl(PipeFormat* fmt);
	~PipeSolverImpl();

	virtual bool computeSat(const Query&);
	virtual bool computeInitialValues(const Query&, Assignment&);

	virtual void printName(int level = 0) const
	{
		klee_message(
			(std::string("%*s PipeSolverImpl(")+fmt->getName()+
			") ").c_str(),
			2*level, "");
	}

	void setTimeout(double in_timeout) { timeout = in_timeout; }
private:
	PipeSolverSession* setupCachedSolver(const char** argv);
	PipeFormat	*fmt;
	std::map<const char**, PipeSolverSession*> cached_sessions;
	double		timeout;
	static uint64_t	prefork_misses;
	static uint64_t prefork_hits;
};
}
#endif
