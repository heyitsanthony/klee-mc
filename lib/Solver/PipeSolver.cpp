#include "klee/SolverStats.h"
#include "klee/Internal/ADT/LimitedStream.h"
#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <wait.h>
#include <fcntl.h> // for F_{SET,GET}PIPE_SZ
#include <sys/prctl.h>
#include "static/Sugar.h"
#include "klee/Constraints.h"
#include "klee/SolverStats.h"
#include "klee/TimerStatIncrementer.h"
#include "klee/util/Assignment.h"
#include <llvm/Support/CommandLine.h>
#include "klee/klee.h"
#include <iostream>

#include "klee/Solver.h"
#include "SMTPrinter.h"
#include "PipeSolver.h"

using namespace klee;
using namespace llvm;

uint64_t PipeSolverImpl::prefork_misses = 0;
uint64_t PipeSolverImpl::prefork_hits = 0;

/* terminates writer process */
static void query_writer_alarm(int x) { _exit(1); }
static void parent_query_writer_alarm(int x) { }

#define MAX_DUMP_BYTES	(1024*1024)
#define TAG "[PipeSolver] "

namespace {
	cl::opt<bool>
	DumpSnooze(
		"dump-snooze",
		cl::desc("DUmp snoozing queries to snooze.<hash>.smt"),
		cl::init(false));

	cl::opt<bool>
	ForkQueries(
		"pipe-fork-queries",
		cl::desc("Fork query writer before sending to pipe."),
		cl::init(false));

	cl::opt<bool>
	LargePipes(
		"large-pipes",
		cl::desc("Force pipe buffers larger than the default 64KB."),
		cl::init(true));

	cl::opt<bool>
	DebugPipeWriteBlock(
		"debug-pipe-write-block",
		cl::desc("Write query blocks forever."),
		cl::init(false));

	cl::opt<bool>
	DebugPipeReadBlock(
		"debug-pipe-read-block",
		cl::desc("Read query response forever."),
		cl::init(false));

	cl::opt<bool>
	DebugWriteRecvQuery(
		"debug-pipe-wrq",
		cl::desc("Debug WriteRecvQuery with pipe-fork"),
		cl::init(false));

	cl::opt<bool>
	PreforkSolver(
		"prefork-solver",
		cl::desc("exec() new solver at query fini."),
		cl::init(true));
}

static void dump_badquery(const Query& q, const char* prefix)
{
	char			fname[256];
	sprintf(fname, "%s.%lx.smt", prefix, q.hash());
	limited_ofstream	lofs(fname, MAX_DUMP_BYTES);
	SMTPrinter::print(lofs, q);
}

PipeSolver::PipeSolver(PipeFormat* in_fmt)
: TimedSolver(new PipeSolverImpl(in_fmt))
{}

PipeSolver::~PipeSolver(void) {}

void PipeSolver::setTimeout(double in_timeout)
{
	static_cast<PipeSolverImpl*>(impl)->setTimeout(in_timeout);
}

PipeSolverImpl::PipeSolverImpl(PipeFormat* in_fmt)
: fmt(in_fmt)
, timeout(-1.0)
{
	assert (fmt);
	/* if solver gets stops accepting our input /
	 * dies, we'll be left writing to a dead pipe which will
	 * raise SIGPIPE. For some reason the interruption code
	 * includes SIGPIPE, so we'll ignore it. In the future
	 * it might be worth it to flip some flag to determine whether
	 * we should ignore (e.g. failure in pipe solver) or should
	 * terminate (e.g. error outside pipe solver) */
	signal(SIGPIPE, SIG_IGN);
}

namespace klee {
class PipeSolverSession
{
public:
	static PipeSolverSession* create(const char* solver_fname, const char **argv);
	virtual ~PipeSolverSession(void) { stop(); }
	void stop();
	bool getSAT(const Query& q, PipeFormat* fmt, double timeout);
	bool getModel(const Query& q, PipeFormat* fmt, double timeout);
protected:
	PipeSolverSession(int child_stdin, int child_stdout, int cpid)
	: fd_child_stdin(child_stdin), fd_child_stdout(child_stdout)
	, child_pid(cpid)
	, stdout_buf(nullptr)
	{}
	bool writeQuery(const Query& q);
	bool writeQueryToChild(const Query& q);
	bool waitOnSolver(const Query& q) const;
	std::istream* writeRecvQuery(const Query& q);
	void doneWriting(void);
private:
	int		fd_child_stdin;
	int		fd_child_stdout;
	pid_t		child_pid;
	__gnu_cxx::stdio_filebuf<char> *stdout_buf;
	double		timeout;
};
}

bool PipeSolverSession::getSAT(const Query& q, PipeFormat* fmt, double to)
{
	bool		parse_ok;
	std::istream	*is;

	timeout = to;
	if (!(is = writeRecvQuery(q.negateExpr()))) {
		stop();
		return false;
	}

	parse_ok = fmt->parseSAT(*is);
	delete is;
	stop();
	return parse_ok;
}

bool PipeSolverSession::getModel(const Query& q, PipeFormat* fmt, double to)
{
	std::istream *is;
	bool parse_ok;

	timeout = to;
	if (!(is = writeRecvQuery(q))) {
		if (!ForkQueries || DebugWriteRecvQuery) {
			dump_badquery(q, "badwrite");
		} else {
			std::cerr << TAG"SUPPRESSING FORKQUERY ERROR\n";
		}
		std::cerr << TAG"FAILED TO WRITERECVQUERY ("
			<< (void*)q.hash() << ")\n";
		stop();
		return false;
	}

	parse_ok = fmt->parseModel(*is);
	delete is;
	stop();
	return parse_ok;
}

bool PipeSolverSession::waitOnSolver(const Query& q) const
{
	struct timeval	tv;
	fd_set		rdset;
	int		rc;

	if (timeout <= 0.0) {
		return true;
	}

	tv.tv_sec = (time_t)timeout;
	tv.tv_usec = (timeout - tv.tv_sec)*1000000;

	FD_ZERO(&rdset);
	FD_SET(fd_child_stdout, &rdset);

	rc = select(fd_child_stdout+1, &rdset, NULL, NULL, &tv);
	if (rc == -1)
		return false;

	if (rc == 0) {
		/* timeout */
		if (DumpSnooze)
			SMTPrinter::dump(q, "snooze");
		return false;
	}

	assert (rc == 1);
	return true;
}

bool PipeSolverSession::writeQuery(const Query& q)
{
	pid_t		query_writer_pid;
	int		status, wait_pid;

	if (!ForkQueries)
		return writeQueryToChild(q);

	/* fork() if requested. This should get around crashes
	 * for really big queries with qemu */
	query_writer_pid = fork();
	if (query_writer_pid == -1)
		return false;

	if (query_writer_pid == 0) {
		/* child writes to solver's stdin */
		prctl(PR_SET_PDEATHSIG, SIGKILL);
		if (timeout > 0.0) {
			signal(SIGALRM, query_writer_alarm);
			alarm((unsigned int)timeout);
		}
		writeQueryToChild(q);
		_exit(0);
	} else {
		/* child handles writes so no need to keep write fd open */
		doneWriting();
	}
	assert (query_writer_pid > 0 && "Parent with bad pid");

	if (timeout > 0.0) {
		/* this sigaction stuff is voodoo from lkml */
		struct sigaction 	sa, old_sa;
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = parent_query_writer_alarm;
		sa.sa_flags = SA_NOMASK;
		sigaction(SIGALRM, &sa, &old_sa);
		alarm((unsigned int)timeout);
		wait_pid = waitpid(query_writer_pid, &status, 0);
		alarm(0); // unschedule alarm
		sigaction(SIGALRM, &old_sa, NULL);
	} else {
		wait_pid = waitpid(query_writer_pid, &status, 0);
	}

	if (wait_pid != query_writer_pid || !WIFEXITED(status)) {
		kill(SIGKILL, query_writer_pid);
		return false;
	}

	if (WEXITSTATUS(status) != 0)
		return false;

	return true;
}

bool PipeSolverSession::writeQueryToChild(const Query& q)
{
	__gnu_cxx::stdio_filebuf<char> stdin_buf(fd_child_stdin, std::ios::out);
	std::ostream *os = new std::ostream(&stdin_buf);
	bool ok;

	os->rdbuf()->pubsetbuf(NULL, 1024*256);
	assert (os->fail() == false);

	/* write it all */
	SMTPrinter::print(*os, q);
	ok = !os->fail();
	if (!ok) {
		std::cerr << TAG"FAILED TO COMPLETELY SEND SMT\n";
		if (!ForkQueries)
			dump_badquery(q, "badsend");
	} else {
		ok = os->flush();
		if (!ok) {
			std::cerr << TAG"FAILED TO FLUSH SMT (watchdog?)\n";
		}
	}
	delete os;

	doneWriting();
	return ok;
}

void PipeSolverSession::doneWriting(void)
{
	close(fd_child_stdin);
	fd_child_stdin = -1;
}

std::istream* PipeSolverSession::writeRecvQuery(const Query& q)
{
	bool	wrote_query;

	++stats::queries;

	wrote_query = writeQuery(q);

	if (!wrote_query) return nullptr;

	assert (stdout_buf == NULL);

	/* wait for data to become available on the pipe.
	 * If none becomes available after a certain time, timeout. */
	if (waitOnSolver(q) == false) return nullptr;

	/* read response, if any */
	stdout_buf = new __gnu_cxx::stdio_filebuf<char>(
		fd_child_stdout, std::ios::in);
	return new std::istream(stdout_buf);
}

void PipeSolverSession::stop(void)
{
	// XXX why do I need ot track this-- why not tracked by istream?
	if (stdout_buf) {
		delete stdout_buf;
		stdout_buf = NULL;
	}

	if (fd_child_stdin != -1) close(fd_child_stdin);
	if (fd_child_stdout != -1) close(fd_child_stdout);
	if (child_pid != -1) {
		int status;
		kill(child_pid, SIGKILL);
		waitpid(child_pid, &status, 0);
	}

	child_pid = -1;
	fd_child_stdin = -1;
	fd_child_stdout = -1;
}

/* create solver child process with stdin/stdout pipes
 * for sending/receiving query expressions/models
 */
PipeSolverSession* PipeSolverSession::create(
	const char* solver_fname, const char** argv)
{
	int	rc;
	int	parent2child[2]; // child reads from [0], parent writes [1]
	int	child2parent[2]; // parent reads from [0], child writes [1]

	assert (argv);
	if (	pipe2(parent2child, O_CLOEXEC) == -1 ||
		pipe2(child2parent, O_CLOEXEC) == -1)
	{
		klee_warning_once(0, "Bad pipes in PipeSolver");
		close(parent2child[0]);
		close(parent2child[1]);
		return nullptr;
	}

	if (LargePipes) {
		rc = fcntl(parent2child[1], F_SETPIPE_SZ, 1024*1024);
		rc |= fcntl(child2parent[1], F_SETPIPE_SZ, 1024*1024);

		if (rc != 1024*1024) {
			klee_warning_once(0, "Solver could not use LargePipes");
			LargePipes = false;
		}
	}

	pid_t child_pid = fork();
	if (child_pid == 0) {
		/* child - stupid unix trivia follows */

		/* kill external solver if we die */
		/* XXX: would like to do this portably, but it seems like
		 * the pure-posix solution is a mess */
		prctl(PR_SET_PDEATHSIG, SIGKILL);

		/* child reads from this */
		if (DebugPipeWriteBlock) {
			/* Create a dummy pipe for solver to read.
			 * In effect, any write to the solver block forever */
			int dummy_pipe[2];
			pipe(dummy_pipe);
			close(STDIN_FILENO);
			rc = dup(dummy_pipe[0]);
			// and keep this around so we don't write to a broken pipe
			dup(parent2child[0]);
		} else {
			close(STDIN_FILENO);
			rc = dup(parent2child[0]);
		}

		// parent2child[0] -> fd=0
		assert (rc == STDIN_FILENO);

		/* child writes to this */
		if (DebugPipeReadBlock) {
			/* Create a dummy pipe for solver to write.
			 * In effect, any reads from the solver block forever */
			int dummy_pipe[2];
			pipe(dummy_pipe);
			close(STDOUT_FILENO);
			rc = dup(dummy_pipe[1]);
		} else {
			close(STDOUT_FILENO);
			rc = dup(child2parent[1]);
		}
		// child2parent[1] -> fd=1
		assert (rc == STDOUT_FILENO);

		// If we don't close the unused pipes, things will block.
		close(STDERR_FILENO);
		close(parent2child[0]);
		close(parent2child[1]);
		close(child2parent[1]);
		close(child2parent[0]);

		char *const *x = (char *const *)argv;
		execvp(solver_fname, x);
		fprintf(stderr, "Bad exec: exec_fname = %s\n", solver_fname);
		assert (0 == 1 && "Failed to execve!");
	} else if (child_pid == -1) {
		/* error */
		assert (0 == 1 && "Bad fork");
		close(parent2child[0]);
		close(parent2child[1]);
		close(child2parent[0]);
		close(child2parent[1]);
		return nullptr;
	}

	/* parent */
	close(parent2child[0]);
	close(child2parent[1]);

	if (DebugPipeWriteBlock) {
		/* Setup blocked pipe for klee to write */
		int	sz;
		int	flags;

		/* fill up buffer so it immediately blocks */
		flags = fcntl(parent2child[1], F_GETFL);
		rc = fcntl(parent2child[1], F_SETFL, flags | O_NONBLOCK);
		assert (rc == 0);

		errno = 0;
		do {
			sz = write(parent2child[1], "a", 1024);
		} while (sz == 1024);

		rc = fcntl(parent2child[1], F_SETFL, flags);
		assert (rc == 0);
	}

	return new PipeSolverSession(parent2child[1], child2parent[0], child_pid);
}

bool PipeSolverImpl::computeInitialValues(const Query& q, Assignment& a)
{
	TimerStatIncrementer	t(stats::queryTime);
	bool			parse_ok, is_sat;
	PipeSolverSession	*pss;
	
	if (!(pss = setupCachedSolver(fmt->getArgvModel()))) {
		failQuery();
		SMTPrinter::dump(q, "badsetup");
		std::cerr << TAG"FAILED TO SETUP FCHILD CIV\n";
		return false;
	}

	parse_ok = pss->getModel(q, fmt, timeout);
	delete pss;
	if (parse_ok == false) {
		std::cerr << TAG"BAD PARSE computeInitialValues ("
			<< (void*)q.hash() << ")\n";
		SMTPrinter::dump(q, "badparse");
		failQuery();
		return false;
	}

	forall_drain (it, a.freeBegin(), a.freeEnd()) {
		std::vector<unsigned char>	v;
		fmt->readArray(*it, v);
		a.bindFree(*it, v);
	}

	is_sat = fmt->isSAT();
	if (is_sat) ++stats::queriesValid;
	else ++stats::queriesInvalid;

	return is_sat;
}

bool PipeSolverImpl::computeSat(const Query& q)
{
	TimerStatIncrementer	t(stats::queryTime);
	bool			parse_ok, is_sat;
	PipeSolverSession	*pss;

	if (!(pss = setupCachedSolver(fmt->getArgvSAT()))) {
		std::cerr << TAG"FAILED COMPUTE SAT QUERY SETUP\n";
		failQuery();
		return false;
	}

	parse_ok = pss->getSAT(q, fmt, timeout);
	delete pss;
	if (!parse_ok) {
		std::cerr << TAG"BAD PARSE SAT (" << (void*)q.hash() << ")\n";
		failQuery();
		if (!ForkQueries || DebugWriteRecvQuery)
			dump_badquery(q, "badsat");
		return false;
	}

	is_sat = fmt->isSAT();
	if (is_sat)	++stats::queriesValid;
	else		++stats::queriesInvalid;

	return is_sat;
}

PipeSolverSession* PipeSolverImpl::setupCachedSolver(const char** argv)
{
	/* no preforked solver => no cached solver; create fresh solver */
	if (PreforkSolver == false)
		return PipeSolverSession::create(fmt->getExec(), argv);

	/* no preforked solver process; create fresh solver */
	auto it(cached_sessions.find(argv));
	if (it == cached_sessions.end() || !it->second) {
		prefork_misses++;
		cached_sessions[argv] = PipeSolverSession::create(fmt->getExec(), argv);
		return PipeSolverSession::create(fmt->getExec(), argv);
	}

	prefork_hits++;

	/* return cached session for immediate use; fork off another session in bg */
	PipeSolverSession* session(it->second);
	cached_sessions[argv] = PipeSolverSession::create(fmt->getExec(), argv);
	return session;
}

PipeSolverImpl::~PipeSolverImpl(void)
{
	for (auto& p : cached_sessions) delete p.second;
	cached_sessions.clear();
	delete fmt;
}
