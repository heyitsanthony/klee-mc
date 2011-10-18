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
	bool setupChild(const char* exec_fname, char* const argv[]);
	void finiChild(void);
	std::istream* writeRecvQuery(const Query& q);
	bool writeQuery(const Query& q) const;
	bool writeQueryToChild(const Query& q) const;
	bool waitOnSolver(const Query& q) const;

	PipeFormat	*fmt;
	int		fd_child_stdin;
	int		fd_child_stdout;
	pid_t		parent_pid;
	pid_t		child_pid;
	__gnu_cxx::stdio_filebuf<char> *stdout_buf;

	double		timeout;
};
}
#endif
