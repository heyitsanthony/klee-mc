#include "klee/SolverStats.h"
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <wait.h>
#include <sys/prctl.h>
#include "static/Sugar.h"
#include "klee/Constraints.h"
#include "klee/SolverStats.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/klee.h"
#include <iostream>
#include "klee/Solver.h"
#include "SMTPrinter.h"
#include "PipeSolver.h"

using namespace klee;

PipeSolver::PipeSolver(PipeFormat* in_fmt)
: TimedSolver(new PipeSolverImpl(in_fmt))
{
}

PipeSolver::~PipeSolver(void) {}


PipeSolverImpl::PipeSolverImpl(PipeFormat* in_fmt)
: fmt(in_fmt)
, fd_child_stdin(-1)
, fd_child_stdout(-1)
, child_pid(-1)
, stdout_buf(NULL)
{
	assert (fmt);
	parent_pid = getpid();
}

PipeSolverImpl::~PipeSolverImpl(void)
{
	finiChild();
	delete fmt;
}

bool PipeSolverImpl::setupChild(const char* exec_fname, char *const argv[])
{
	int	rc;
	int	parent2child[2]; // child reads from [0], parent writes [1]
	int	child2parent[2]; // parent reads from [0], child writes [1]

	assert (argv);
	assert (fd_child_stdin == -1 && fd_child_stdout == -1);

	if (	pipe(parent2child) == -1 ||
		pipe(child2parent) == -1)
	{
		klee_warning_once(0, "Bad pipes in PipeSolver");
		failQuery();
		return false;
	}

	child_pid = fork();
	if (child_pid == 0) {
		/* child - stupid unix trivia follows */

		/* kill externaol solver if we die */
		/* XXX: would like to do this portably, but it seems like
		 * the pure-posix solution is a mess */
		prctl(PR_SET_PDEATHSIG, SIGKILL);

		/* child reads from this */
		close(STDIN_FILENO);
		rc = dup(parent2child[0]);
		close(parent2child[1]);
		assert (rc == STDIN_FILENO);

		/* child writes to this */
		close(STDOUT_FILENO);
		rc = dup(child2parent[1]);
		close(child2parent[0]);
		assert (rc == STDOUT_FILENO);

		execvp(exec_fname, argv);
		fprintf(stderr, "Bad exec: exec_fname = %s\n", exec_fname);
		assert (0 == 1 && "Failed to execve!");
	} else if (child_pid == -1) {
		/* error */
		failQuery();
		assert (0 == 1 && "Bad fork");
		close(parent2child[0]);
		close(parent2child[1]);
		close(child2parent[0]);
		close(child2parent[1]);
		return false;
	}

	/* parent */
	close(parent2child[0]);
	close(child2parent[1]);

	fd_child_stdin = parent2child[1];
	fd_child_stdout = child2parent[0];

	return true;
}

void PipeSolverImpl::finiChild(void)
{
	if (stdout_buf) {
		delete stdout_buf;
		stdout_buf = NULL;
	}
	if (fd_child_stdin != -1) close(fd_child_stdin);
	if (fd_child_stdout != -1) close(fd_child_stdout);
	if (child_pid != -1) {
		int status;
		waitpid(child_pid, &status, 0);
	}

	child_pid = -1;
	fd_child_stdin = -1;
	fd_child_stdout = -1;
}

bool PipeSolverImpl::computeInitialValues(
	const Query& q,
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values)
{
	TimerStatIncrementer	t(stats::queryTime);
	bool			parse_ok, is_sat;

	values.clear();
	values.resize(objects.size());

	if (setupChild(
		fmt->getExec(),
		const_cast<char* const*>(fmt->getArgvModel())) == false)
	{
		failQuery();
		std::cerr << "FAILED TO SETUP FCHILD CIV\n";
		return false;
	}

	std::istream *is = writeRecvQuery(q);
	if (!is) return false;

	parse_ok = fmt->parseModel(*is);
	delete is;

	finiChild();

	if (parse_ok == false) {
		std::cerr << "BAD PARSE CIV\n";
		failQuery();
		return false;
	}

	unsigned int i = 0;
	foreach (it, objects.begin(), objects.end()) {
		fmt->readArray(*it, values[i++]);
	}

	is_sat = fmt->isSAT();
	if (is_sat)
		++stats::queriesValid;
	else
		++stats::queriesInvalid;

	return is_sat;
}

bool PipeSolverImpl::computeSat(const Query& q)
{
	TimerStatIncrementer	t(stats::queryTime);
	bool			parse_ok, is_sat;

	if (setupChild(fmt->getExec(),
		const_cast<char* const*>(fmt->getArgvSAT())) == false) {
		std::cerr << "FAILED COMPUTE SAT QUERY\n";
		failQuery();
		return false;
	}

	std::istream	*is = writeRecvQuery(q.negateExpr());
	if (!is) return false;

	parse_ok = fmt->parseSAT(*is);
	delete is;

	finiChild();

	if (parse_ok == false) {
		std::cerr << "BAD PARSE SAT\n";
		failQuery();
		return false;
	}

	is_sat = fmt->isSAT();
	if (is_sat)
		++stats::queriesValid;
	else
		++stats::queriesInvalid;

	return is_sat;
}

std::istream* PipeSolverImpl::writeRecvQuery(const Query& q)
{
	std::istream *is;

	__gnu_cxx::stdio_filebuf<char> stdin_buf(fd_child_stdin, std::ios::out);
	std::ostream *os = new std::ostream(&stdin_buf);

	/* write it all */
	SMTPrinter::print(*os, q);
	if (os->fail()) {
		std::cerr << "FAILED TO COMPLETELY SEND SMT\n";
		return NULL;
	}
	os->flush();
	delete os;

	close(fd_child_stdin);
	fd_child_stdin = -1;

	/* read response, if any */
	assert (stdout_buf == NULL);
	stdout_buf = new __gnu_cxx::stdio_filebuf<char>(
		fd_child_stdout, std::ios::in);
	is = new std::istream(stdout_buf);

	return is;
}
