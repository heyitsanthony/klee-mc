#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_os_ostream.h>

#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Replay.h"
#include "klee/Common.h"
#include "Executor.h"
#include "Terminator.h"
#include "StateSolver.h"

#include <sstream>

using namespace klee;
using namespace llvm;

namespace llvm
{
#define DECL_OPTBOOL(x,y)	cl::opt<bool> x(y, cl::init(false))
  DECL_OPTBOOL(EmitOnlyFirstNewCov, "emit-only-first-newcov");
  DECL_OPTBOOL(EmitOnlyBrUncommitted, "emit-only-bruncommitted");
  DECL_OPTBOOL(EmitOnlyCovSetUncommitted, "emit-only-covset-uncommitted");
  DECL_OPTBOOL(EmitEarly, "emit-early");

  cl::opt<bool>
  ConcretizeEarlyTerminate(
  	"concretize-early",
	cl::desc("Concretizee early terminations"),
	cl::init(true));

  cl::opt<bool>
  EmitAllErrors(
  	"emit-all-errors",
        cl::desc("Generate tests cases for all errors "
        	"(default=one per (error,instruction) pair)"));
}

bool Terminator::isInteresting(ExecutionState& st) const
{
	if (EmitOnlyBrUncommitted && !Replay::isCommitted(*exe, st))
		return false;

	if (EmitOnlyFirstNewCov && st.coveredNew == 0)
		return false;

	if (EmitOnlyCovSetUncommitted && st.covset.isCommitted())
		return false;

	return true;
}

bool TermExit::terminate(ExecutionState& state)
{
	state.isPartial = false;
	return true;
}

void TermExit::process(ExecutionState& state)
{ getExe()->getInterpreterHandler()->processTestCase(state, 0, 0); }

typedef std::pair<CallStack::insstack_ty, std::string> errmsg_ty;
std::set<errmsg_ty> emittedErrors;
bool TermError::isInteresting(ExecutionState& state) const
{
	CallStack::insstack_ty	ins;

	/* It can be annoying to emit errors with the same trace. */
	if (alwaysEmit || EmitAllErrors)
		return true;

	ins = state.stack.getKInstStack();
	ins.push_back(state.prevPC);

	if (emittedErrors.emplace(ins, messaget).second == false)
		return false;

	return true;
}

bool TermError::terminate(ExecutionState& state)
{
	state.isPartial = false;
	getExe()->getInterpreterHandler()->incErrorsFound();
	return true;
}

void TermError::process(ExecutionState& state)
{
	std::ostringstream msg;
	printStateErrorMessage(state, messaget, msg);

	if (info != "") msg << "Info: \n" << info;

	getExe()->getInterpreterHandler()->processTestCase(
		state, msg.str().c_str(), suffix.c_str());
}


static int	call_depth = 0;
bool TermEarly::terminate(ExecutionState &state)
{
	ExecutionState	*term_st;

	call_depth++;
	term_st = &state;

	/* call, concretize the state; subsequent, kill it */
	if (	ConcretizeEarlyTerminate &&
		!getExe()->isHalted() &&
		call_depth == 1 && !state.isConcrete())
	{
		ExecutionState	*sym_st;

		/* timed out on some instruction-- back it up */
		state.abortInstruction();

		sym_st = getExe()->concretizeState(
			state, getExe()->getSolver()->getLastBadExpr());
		if (sym_st != NULL) {
			term_st = sym_st;
		}
	}

	term_st->isPartial = true;


	call_depth--;

	if (term_st == &state)
		return true;

	/* nothing. */
	if (isInteresting(*term_st))
		process(*term_st);

	getExe()->terminate(*term_st);
	return false;
}

void TermEarly::process(ExecutionState& state)
{
	std::stringstream	ss;

	ss << message << '\n';
	getExe()->printStackTrace(state, ss);
	getExe()->getInterpreterHandler()->processTestCase(
		state, ss.str().c_str(), "early");
}

bool TermEarly::isInteresting(ExecutionState& st) const
{
	return (EmitEarly)
		? Terminator::isInteresting(st)
		: false;
}

void TermError::printStateErrorMessage(
	ExecutionState& state,
	const std::string& message,
	std::ostream& os)
{
	const InstructionInfo &ii = *state.prevPC->getInfo();
	if (ii.file != "") {
		klee_message("ERROR: %s:%d: %s",
			ii.file.c_str(),
			ii.line,
			message.c_str());
	} else {
		klee_message("ERROR: %s", message.c_str());
	}

	if (!EmitAllErrors)
		klee_message("NOTE: now ignoring this error at this location");

	os << "Error: " << message << "\n";
	if (ii.file != "") {
		os << "File: " << ii.file << "\n";
		os << "Line: " << ii.line << "\n";
	}

	os << "Stack: \n";
	getExe()->printStackTrace(state, os);

	if (state.prevPC && state.prevPC->getInst()) {
		raw_os_ostream	ros(os);
		ros << "problem PC:\n";
		ros << *(state.prevPC->getInst());
		ros << "\n";
	}
}
