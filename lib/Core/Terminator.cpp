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

	if (EmitOnlyCovSetUncommitted && st.covset.isCommitted()) {
		std::cerr << "[Term] State's coverage already committed.\n";
		return false;
	}

	return true;
}

bool TermExit::terminate(ExecutionState& state)
{
	state.isPartial = false;
	return true;
}

void TermExit::process(ExecutionState& state)
{ getExe()->getInterpreterHandler()->processTestCase(state, 0, 0); }


std::set<TermError::errmsg_ty> TermError::emittedErrors;

TermError::TermError(
	Executor* _exe,
	const ExecutionState &state,
	const std::string &_msgt,
	const std::string& _suffix,
	const std::string &_info,
	bool _alwaysEmit)
: Terminator(_exe)
, messaget(_msgt)
, suffix(_suffix)
, info(_info)
, alwaysEmit(_alwaysEmit)
, ins(state.stack.getKInstStack())
{
	ins.push_back(state.prevPC);
}

TermError::TermError(const TermError& te)
: Terminator(te.getExe())
, messaget(te.messaget)
, suffix(te.suffix)
, info(te.info)
, alwaysEmit(te.alwaysEmit)
, ins(te.ins)
{
}

bool TermError::isInteresting(ExecutionState& state) const
{
	/* It can be annoying to emit errors with the same trace. */
	if (alwaysEmit || EmitAllErrors)
		return true;

	if (emittedErrors.count({ins, messaget})) {
		std::cerr << "[Term] Error " << messaget << " already found.\n";
		return false;
	}
	// Can't insert to emittedErrors here because the Fini functions
	// could terminate the state and the error will never be emitted.
	//
	// On the other hand, I don't think the stack trace after Fini
	// will match what's used here, so the check is useless for doing
	// more fini stuff.

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

	if (emittedErrors.emplace(ins, messaget).second == false) {
		std::cerr << "[Term] Emplaced error " << messaget << "\n";
	} else {
		std::cerr << "[Term] Error already exists " << messaget << "\n";
	}

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
			/* destroy the old symbolic state that can't make
			 * any progress... */
			term_st = sym_st;
		}
	}

	term_st->isPartial = true;


	call_depth--;

	if (term_st == &state)
		return true;

	if (isInteresting(*term_st)) {
		std::cerr << "[Term] Processing bad symbolic state.\n";
		process(*term_st);
	}

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
