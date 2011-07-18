#include "klee/SolverFormat.h"
#include "klee/SolverStats.h"
#include "STPSolver.h"
#include "STPBuilder.h"

#include "llvm/Support/CommandLine.h"
#include "static/Sugar.h"
#include "klee/util/Assignment.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprUtil.h"
#include "klee/Constraints.h"
#include "klee/TimerStatIncrementer.h"

#include <sys/time.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>

using namespace llvm;
using namespace klee;

namespace
{
  cl::opt<bool>
  DebugPrintQueries("debug-print-queries",
                    cl::desc("Print queries during execution."));
}


static unsigned char	*shared_memory_ptr;
static const unsigned	shared_memory_size = 1<<20;
static int		shared_memory_id;

static bool sendall(int fd, const void* vp, size_t sz);
static bool recvall(int fd, void* vp, size_t sz, bool timeout, timeval& tExpire);
static int strcmp_objName(const char* s1, const char* s2);
template<typename F>
int runForked(const char* name, F& f, unsigned timeout, sighandler_t handler);


static void stp_error_handler(const char* err_msg)
{
  fprintf(stderr, "error: STP Error: %s\n", err_msg);
  abort();
}

class ResultHolder
{
public:
  virtual ~ResultHolder() { }
  virtual void reserve(size_t sz) = 0;
  virtual void newValue(size_t sz) = 0;
  virtual void newByte(uint8_t val) = 0;
};

class ResultHolder_Vector : public ResultHolder
{
  typedef std::vector<uint8_t> Value;
  typedef std::vector<Value> Values;
  Values& values;

public:
  ResultHolder_Vector(Values& v) : values(v) { values.clear(); }
  void reserve(size_t sz) { values.reserve(sz); }
  void newValue(size_t sz) { values.push_back(Value()); values.back().reserve(sz); }
  void newByte(uint8_t val) { values.back().push_back(val); }
};

class ResultHolder_Stream : public ResultHolder
{
  unsigned char* pos;

public:
  ResultHolder_Stream(unsigned char* p) : pos(p) { }
  void reserve(size_t sz) { }
  void newValue(size_t sz) { }
  void newByte(uint8_t val) { *pos++ = val; }
};

static int runAndGetCex(
	::VC vc,
	STPBuilder *builder,
	::VCExpr q,
	const std::vector<const Array*> &objects,
        ResultHolder& rh,
	bool &hasSolution)
{
	// XXX I want to be able to timeout here, safely
	int	res;

	res = vc_query(vc, q);
	hasSolution = !res;
	if (!hasSolution) return res;

	rh.reserve(objects.size());

	// FIXME: this can potentially take a *long* time
	foreach (it, objects.begin(), objects.end()) {
		const Array *array = *it;

		rh.newValue(array->mallocKey.size);
		for (	unsigned offset = 0;
			offset < array->mallocKey.size;
			offset++)
		{
			ExprHandle counter;
			unsigned char val;
			counter = vc_getCounterExample(
				vc,
				builder->getInitialRead(array, offset));
			val = getBVUnsigned(counter);
			rh.newByte(val);
		}
	}

	return res;
}

class RunAndGetCexWrapper
{
  ::VC vc_;
  STPBuilder* builder_;
  ::VCExpr q_;
  const std::vector<const Array*>& objects_;
  ResultHolder& rh_;
  bool hasSolution_;

public:
  RunAndGetCexWrapper(::VC vc, STPBuilder* builder, ExprHandle stp_e,
                      const std::vector<const Array*>& objects,
                      ResultHolder& rh) :
    vc_(vc), builder_(builder), q_(stp_e), objects_(objects), rh_(rh)
  { }
  int operator()() {
    return runAndGetCex(vc_, builder_, q_, objects_, rh_, hasSolution_);
  }
};


static const int timeoutExitCode = 52;
static void stpTimeoutHandler(int x) { _exit(timeoutExitCode); }

STPSolverImpl::STPSolverImpl(STPSolver *_solver, bool _useForkedSTP)
  : solver(_solver),
    vc(vc_createValidityChecker()),
    builder(new STPBuilder(vc)),
    timeout(0.0),
    useForkedSTP(_useForkedSTP)
{
  assert(vc && "unable to create validity checker");
  assert(builder && "unable to create STPBuilder");

#ifdef HAVE_EXT_STP
  // In newer versions of STP, a memory management mechanism has been
  // introduced that automatically invalidates certain C interface
  // pointers at vc_Destroy time.  This caused double-free errors
  // due to the ExprHandle destructor also attempting to invalidate
  // the pointers using vc_DeleteExpr.  By setting EXPRDELETE to 0
  // we restore the old behaviour.
  vc_setInterfaceFlags(vc, EXPRDELETE, 0);
#endif
  vc_registerErrorHandler(::stp_error_handler);

  if (useForkedSTP) {
    shared_memory_id = shmget(IPC_PRIVATE, shared_memory_size, IPC_CREAT | 0700);
    assert(shared_memory_id>=0 && "shmget failed");
    shared_memory_ptr = (unsigned char*) shmat(shared_memory_id, NULL, 0);
    assert(shared_memory_ptr!=(void*)-1 && "shmat failed");
    shmctl(shared_memory_id, IPC_RMID, NULL);
  }
}

STPSolverImpl::~STPSolverImpl()
{
  delete builder;
  vc_Destroy(vc);
}

/***/

STPSolver::STPSolver(bool useForkedSTP, sockaddr_in_opt stpServer)
  : Solver(stpServer.null()
      ? new STPSolverImpl(this, useForkedSTP)
      : new ServerSTPSolverImpl(this, useForkedSTP, stpServer))
{
}

char *STPSolver::getConstraintLog(const Query &query) {
  return static_cast<STPSolverImpl*>(impl)->getConstraintLog(query);
}

void STPSolver::setTimeout(double timeout) {
  static_cast<STPSolverImpl*>(impl)->setTimeout(timeout);
}

/***/

char *STPSolverImpl::getConstraintLog(const Query &query)
{
  vc_push(vc);
  foreach (it, query.constraints.begin(), query.constraints.end())
    vc_assertFormula(vc, builder->construct(*it));
  assert(query.expr == ConstantExpr::alloc(0, Expr::Bool) &&
         "Unexpected expression in query!");

  char *buffer;
  unsigned long length;
  vc_printQueryStateToBuffer(vc, builder->getFalse(),
                             &buffer, &length, false);
  vc_pop(vc);

  return buffer;
}

bool STPSolverImpl::computeTruth(
	const Query& query,
	bool &isValid)
{
  std::vector<const Array*> objects;
  std::vector< std::vector<unsigned char> > values;
  bool hasSolution;

  if (!computeInitialValues(query, objects, values, hasSolution))
    return false;

  isValid = !hasSolution;
  return true;
}

bool STPSolverImpl::computeValue(
	const Query& query,
	ref<Expr> &result)
{
  std::vector<const Array*> objects;
  std::vector< std::vector<unsigned char> > values;
  bool hasSolution;

  // Find the object used in the expression, and compute an assignment
  // for them.
  findSymbolicObjects(query.expr, objects);
  if (!computeInitialValues(query.withFalse(), objects, values, hasSolution))
    return false;
  assert(hasSolution && "state has invalid constraint set");

  // Evaluate the expression with the computed assignment.
  Assignment a(objects, values);
  result = a.evaluate(query.expr);

  return true;
}

bool STPSolverImpl::doForkedComputeInitialValues(
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values,
	ExprHandle& stp_e,
	bool& hasSolution)
{
	bool	success;
	size_t	sum = 0;

	foreach (it, objects.begin(), objects.end())
		sum += (*it)->mallocKey.size;
	assert (sum < shared_memory_size
		&& "not enough shared memory for counterexample");

	unsigned char* pos = shared_memory_ptr;
	ResultHolder_Stream rh(pos);
	RunAndGetCexWrapper w(vc, builder, stp_e, objects, rh);
	int exitcode = runForked("STP", w, timeout, stpTimeoutHandler);

	if (exitcode < 0)
		success = false;
	else if (exitcode == 0) {
		hasSolution = true;
		success = true;
	} else if (exitcode == 1) {
		hasSolution = false;
		success = true;
	} else if (exitcode == timeoutExitCode) {
		fprintf(stderr, "error: STP timed out");
		success = false;
	} else {
		fprintf(stderr, "error: STP did not return a recognized code");
		success = false;
	}

	if (!hasSolution) return success;
	if (!success) return false;

	values = std::vector< std::vector<unsigned char> >(objects.size());
	unsigned i=0;
	foreach (it, objects.begin(), objects.end()) {
		const Array *array = *it;
		std::vector<unsigned char> &data = values[i++];
		data.insert(data.begin(), pos, pos + array->mallocKey.size);
		pos += array->mallocKey.size;
	}

	return true;
}

bool ServerSTPSolverImpl::talkToServer(
	double timeout, const char* qstr, unsigned long qlen,
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values,
	bool &hasSolution)
{
  STPReqHdr stpRequest;
  stpRequest.timeout_sec = floor(timeout);
  stpRequest.timeout_usec = floor(1000000 * (timeout - floor(timeout)));
  stpRequest.length = qlen;

  // prepare TCP socket
  class FileDescriptor
  {
    int fd_;
  public:
    FileDescriptor(int n0) : fd_(n0) { }
    int close() { assert(fd_ >= 0); int r = ::close(fd_); fd_ = -1; return r; }
    ~FileDescriptor() { if (fd_ >= 0) close(); }
    operator int() const { return fd_; }
  } fd(socket(PF_INET, SOCK_STREAM, 0));
  if (fd < 0)
    klee_error("socket(): %s", strerror(errno));

  // connect to server
  int r;
  do {
    r = connect(fd, (const sockaddr*) &server.sin, sizeof(server.sin));
  } while (r < 0 && errno == EINTR);
  if (r < 0) {
    klee_warning("cannot connect to %s: %s", server.str.c_str(), strerror(errno));
    return false;
  }

  stpRequest.timeout_sec = htonl(stpRequest.timeout_sec);
  stpRequest.timeout_usec = htonl(stpRequest.timeout_usec);
  stpRequest.length = htonl(stpRequest.length);

  // send query header
  if (!sendall(fd, &stpRequest, sizeof(stpRequest)))
    return false;

  // send query
  if (!sendall(fd, qstr, qlen))
    return false;

  timeval tNow, tExpire, tTimeout;
  tTimeout.tv_sec = floor(timeout);
  tTimeout.tv_usec = floor(1000000 * (timeout - floor(timeout)));
  gettimeofday(&tNow, NULL);
  timeradd(&tNow, &tTimeout, &tExpire);

  // receive response header from server
  STPResHdr cexHeader;
  if (!recvall(fd, &cexHeader, sizeof(cexHeader), timeout, tExpire))
    return false;
  cexHeader.rows = ntohl(cexHeader.rows);

  // parse result
  if (cexHeader.result == 'V')
    hasSolution = false;
  else if (cexHeader.result == 'I')
    hasSolution = true;
  else
    klee_error("Invalid query result");

  // receive and parse counter-example if necessary
  if (!hasSolution) return true;

  // initialize all counter-example values to 0
  ResultHolder_Vector rh(values);
  rh.reserve(objects.size());

  foreach (it, objects.begin(), objects.end()) {
    const Array *array = *it;
    rh.newValue(array->mallocKey.size);
    // XXX is dereferencing obj threadsafe???
    for (unsigned offset = 0; offset < array->mallocKey.size; offset++)
      rh.newByte(0);
  }

  // receive counter-examples
  for (unsigned i = 0; i < cexHeader.rows; i++) {
    CexItem cex;
    if (!recvall(fd, &cex, sizeof(cex), timeout, tExpire))
      return false;
    cex.id = ntohl(cex.id);
    cex.offset = ntohl(cex.offset);

    char itemName[128];
    if (cex.id & CexItem::const_flag) {
      cex.id &= ~CexItem::const_flag;
      sprintf(itemName, "const_arr%u", cex.id);
    }
    else
      sprintf(itemName, "arr%u", cex.id);

    // perform linear search for object
    std::vector<const Array*>::size_type j = 0;
    for (; j != objects.size(); ++j) {
      const Array* array = objects[j];
      const char* objName = array->getSTPArrayName();
      // if object hasn't been used in a query expression, it doesn't have
      // a name yet
      if (objName && strcmp_objName(objName, itemName) == 0) break;
    }
    if (j != objects.size()) {
      // set value for counter-example
      values[j].at(cex.offset) = cex.value;
    }
  }

  return true;
}

bool ServerSTPSolverImpl::computeInitialValues(
	const Query& query,
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values,
	bool &hasSolution)
{
  TimerStatIncrementer t(stats::queryTime);
  std::ostream& os = std::clog;

  vc_push(vc);

  foreach (it, query.constraints.begin(), query.constraints.end())
    vc_assertFormula(vc, builder->construct(*it));

  ++stats::queries;
  ++stats::queryCounterexamples;

  ExprHandle stp_e = builder->construct(query.expr);
  if (DebugPrintQueries) {
    os << "\n-- STP CounterExample --\n";
    ExprPPrinter::printQuery(os, query.constraints, query.expr);
    os.flush();
  }

  // get CVC representation for STP query
  char* qstr;
  unsigned long qlen;

  vc_printQueryStateToBuffer(vc, stp_e, &qstr, &qlen, false);

  vc_pop(vc);

  // don't send null terminator (STP's yacc rejects it)
  qlen--;

  bool success = talkToServer(timeout, qstr, qlen, objects, values, hasSolution);

  free(qstr);

  if (!success)
    return false;

  // update stats
  if (!hasSolution) /* cexHeader.cResult == 'V' */
    ++stats::queriesValid;
  else /* cexHeader.cResult == 'I' */
    ++stats::queriesInvalid;

  if (DebugPrintQueries)
    printDebugQueries(os, t.check(), objects, values, hasSolution);

  return true;
}

#if INT_MAX <= UCHAR_MAX
#error "assumption (INT_MAX > UCHAR_MAX) failed"
#endif

static int strcmp_objName(const char* s1, const char* s2)
// Compare the strings pointed by s1 and s2,
// assuming that the former terminates at the first '_'.
{
  const unsigned char* u1 = reinterpret_cast<const unsigned char*>(s1);
  const unsigned char* u2 = reinterpret_cast<const unsigned char*>(s2);
  unsigned char v1, v2;
  do {
    v1 = *u1++;
    if (v1 == '_') v1 = '\0';
    v2 = *u2++;
  }
  while (v1 == v2 && v1 != '\0');
  return v1 - v2;
}

static bool sendall(int fd, const void* vp, size_t sz)
{
  const char* cp = static_cast<const char*>(vp);
  ssize_t nl = sz;

  while (nl > 0) {
    ssize_t n;
    do {
      n = send(fd, cp, nl, 0);
    } while (n < 0 && errno == EINTR);
    if (n < 0) {
      klee_warning("error sending query: %s", strerror(errno));
      return false;
    }
    else {
      nl -= n;
      cp += n;
    }
  }

  return nl == 0;
}

static bool recvall(int fd, void* vp, size_t sz, bool timeout, timeval& tExpire)
{
  char* cp = static_cast<char*>(vp);
  ssize_t nl = sz;

  while (nl > 0) {
    if (timeout) {
      struct timeval tNow, tTimeout;
      gettimeofday(&tNow, NULL);
      // timer may have expired while thread slept; so poll one last time
      // before declaring timeout
      if (timercmp(&tNow, &tExpire, >))
        timerclear(&tTimeout);
      else
        timersub(&tExpire, &tNow, &tTimeout);

      struct pollfd pollfd = {fd, POLLIN | POLLPRI | POLLRDHUP, 0};
      int m = poll(&pollfd, 1, tTimeout.tv_sec * 1000 + tTimeout.tv_usec / 1000);

      if (m < 0) {
        if (errno == EINTR)
          continue;
        else {
          klee_warning("error receiving response: %s", strerror(errno));
          return false;
        }
      }
      else if (m == 0) {
        klee_warning("query timed out");
        return false;
      }
    }

    ssize_t n = recv(fd, cp, nl, 0);
    if (n < 0) {
      if (errno == EINTR)
        n = 0;
      else {
        klee_warning("error receiving response: %s", strerror(errno));
        return false;
      }
    }
    else if (n == 0) {
      klee_warning("server disconnected");
      break;
    }
    else {
      nl -= n;
      cp += n;
    }
  }

  return nl == 0;
}

bool STPSolverImpl::computeInitialValues(
	const Query &query,
	const std::vector<const Array*> &objects,
	std::vector< std::vector<unsigned char> > &values,
	bool &hasSolution)
{
  bool success;
  TimerStatIncrementer t(stats::queryTime);
  std::ostream& os = std::clog;

  vc_push(vc);

  foreach (it, query.constraints.begin(), query.constraints.end())
    vc_assertFormula(vc, builder->construct(*it));

  ++stats::queries;
  ++stats::queryCounterexamples;

  ExprHandle stp_e = builder->construct(query.expr);
  if (DebugPrintQueries) {
    os << "\n-- STP CounterExample --\n";
    ExprPPrinter::printQuery(os, query.constraints, query.expr);
    os.flush();
  }

  if (0) {
    char *buf;
    unsigned long len;
    vc_printQueryStateToBuffer(vc, stp_e, &buf, &len, false);
    fprintf(stderr, "note: STP query: %.*s\n", (unsigned) len, buf);
  }

  if (useForkedSTP) {
    success = doForkedComputeInitialValues(objects, values, stp_e, hasSolution);
  } else {
    ResultHolder_Vector rh(values);
    runAndGetCex(vc, builder, stp_e, objects, rh, hasSolution);
    success = true;
  }

  if (success) {
    if (hasSolution)
      ++stats::queriesInvalid;
    else
      ++stats::queriesValid;
  }

  vc_pop(vc);

  if (DebugPrintQueries)
  	printDebugQueries(os, t.check(), objects, values, hasSolution);

  return success;
}

// Returns -1 if failed to complete the child process,
// or non-negative exit status if succeeded.
template<typename F>
int runForked(const char* name, F& f, unsigned timeout, sighandler_t handler)
{
  fflush(stdout);
  fflush(stderr);

  pid_t pid = fork();
  if (pid == -1) {
    klee_warning_once(0, "fork() for %s failed (%s)", name, strerror(errno));
    return -1;
  }
  else if (pid == 0) {
    /* child */
    if (timeout) {
      ::alarm(0); /* Turn off alarm so we can safely set signal handler */
      ::signal(SIGALRM, handler);
      ::alarm(std::max(1U, timeout));
    }
    int res = f();
    _exit(res);
  }
  else {
    /* parent */
    int status;
    int res;

    do {
      res = waitpid(pid, &status, 0);
    } while (res < 0 && errno == EINTR);

    if (res < 0) {
      klee_warning_once(0, "waitpid() for %s failed", name);
      return -1;
    }

    // From timed_run.py: It appears that linux at least will on
    // "occasion" return a status when the process was terminated by a
    // signal, so test signal first.
    if (WIFSIGNALED(status) || !WIFEXITED(status)) {
      klee_warning_once(0, "%s did not return successfully", name);
      return -1;
    }

    int exitcode = WEXITSTATUS(status);
    assert(exitcode >= 0);
    return exitcode;
  }
}


