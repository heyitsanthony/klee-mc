#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_os_ostream.h>
#include "static/Sugar.h"

#include "klee/Internal/Module/KFunction.h"
#include "KleeHandlerVex.h"
#include "ExecutorVex.h"
#include "ExeStateVex.h"

#include "guestcpustate.h"
#include "guestsnapshot.h"

#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>

#include <iostream>
#include <fstream>

using namespace llvm;
using namespace klee;

namespace
{
	llvm::cl::opt<bool> ValidateTestCase(
		"validate-test",
		llvm::cl::desc("Validate tests with concrete replay"));
}

KleeHandlerVex::KleeHandlerVex(const CmdArgs* cmdargs, Guest *_gs)
: KleeHandler(cmdargs), gs(_gs)
{
	if (ValidateTestCase) {
		/* can only validate tests when the guest is a snapshot,
		 * otherwise the values get jumbled up */
		assert (gs != NULL);
		assert (dynamic_cast<GuestSnapshot*>(gs) != NULL);
	}
}


unsigned KleeHandlerVex::processTestCase(
	const ExecutionState &state,
	const char *errorMessage,
	const char *errorSuffix)
{
	std::ostream	*f;
	unsigned	id;

	id = KleeHandler::processTestCase(state, errorMessage, errorSuffix);
	if (!id) return 0;

	dumpLog(state, "crumbs", id);

	fprintf(stderr, "===DONE WRITING TESTID=%d (es=%p)===\n", id, &state);
	if (!ValidateTestCase)
		return id;

	if (getStopAfterNTests() && id >= getStopAfterNTests())
		return id;

	f = openTestFile("validate", id);
	if (f == NULL)
		return id;

	if (validateTest(id))
		(*f) << "OK #" << id << '\n';
	else
		(*f) << "FAIL #" << id << '\n';

	delete f;
	return id;
}

void KleeHandlerVex::dumpLog(
	const ExecutionState& state, const char* name, unsigned id)
{
	const ExeStateVex		*esv;
	RecordLog::const_iterator	begin, end;
	std::ostream			*f;

	esv = dynamic_cast<const ExeStateVex*>(&state);
	assert (esv != NULL);

	begin = esv->crumbBegin();
	end = esv->crumbEnd();
	if (begin == end) return;

	f = openTestFileGZ(name, id);
	if (f == NULL) return;

	foreach (it, begin, end) {
		const std::vector<unsigned char>	&r(*it);
		std::copy(
			r.begin(), r.end(),
			std::ostream_iterator<unsigned char>(*f));
	}

	delete f;
}


void KleeHandlerVex::printErrorMessage(
	const ExecutionState& state,
	const char* errorMessage,
	const char* errorSuffix,
	unsigned id)
{
	KleeHandler::printErrorMessage(state, errorMessage, errorSuffix, id);

	if (std::ostream* f = openTestFileGZ("errdump", id)) {
		printErrDump(state, *f);
		delete f;
	} else
		LOSING_IT("errdump");
}

void KleeHandlerVex::printErrDump(
	const ExecutionState& state,
	std::ostream& os) const
{
	const Function* top_f;
	os << "Stack:\n";
	m_exevex->printStackTrace(state, os);

	os << "\nRegisters:\n";
	gs->getCPUState()->print(os);

	top_f = state.stack.back().kf->function;
	os << "Func: ";
	if (top_f != NULL) {
		raw_os_ostream ros(os);
		ros << top_f;
	} else
		os << "???";
	os << "\n";

	os << "Objects:\n";
	state.addressSpace.printObjects(os);

	os << "Constraints: \n";
	state.constraints.print(os);
}

bool KleeHandlerVex::validateTest(unsigned id)
{
	pid_t	child_pid, ret_pid;
	int	status;

	child_pid = fork();
	if (child_pid == -1) {
		std::cerr << "ERROR: could not validate test " << id << '\n';
		return false;
	}

	if (child_pid == 0) {
		const char	*argv[5];
		char		idstr[32];

		argv[0] = "kmc-replay";

		snprintf(idstr, 32, "%d", id);
		argv[1] = idstr;
		argv[2] = getOutputDir();
		argv[3] = static_cast<GuestSnapshot*>(gs)->getPath().c_str();
		argv[4] = NULL;

		/* don't write anything please! */
		close(0);
		close(1);
		close(2);
		open("/dev/null", O_RDWR);
		open("/dev/null", O_RDWR);
		open("/dev/null", O_RDWR);
		execvp("kmc-replay", (char**)argv); 
		_exit(-1);
	}

	ret_pid = waitpid(child_pid, &status, 0);
	if (ret_pid != child_pid) {
		std::cerr << "VALDIATE: BAD WAIT\n";
		return false;
	}

	if (WIFSIGNALED(status)) {
		int	sig = WTERMSIG(status);
		if (sig == SIGSEGV)
			return true;
		if (sig == SIGFPE)
			return true;
	}

	if (!WIFEXITED(status)) {
		std::cerr << "VALIDATE: DID NOT EXIT\n";
		return false;
	}

	if (WEXITSTATUS(status) != 0) {
		std::cerr << "VALIDATE: BAD RETURN\n";
		return false;
	}

	return true;
}

void KleeHandlerVex::setInterpreter(Interpreter *i)
{
	m_exevex = dynamic_cast<ExecutorVex*>(i);
	assert (m_exevex != NULL && "Expected ExecutorVex interpreter");

	KleeHandler::setInterpreter(i);
}

