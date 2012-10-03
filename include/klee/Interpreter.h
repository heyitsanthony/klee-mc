//===-- Interpreter.h - Abstract Execution Engine Interface -----*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//===----------------------------------------------------------------------===//

#ifndef KLEE_INTERPRETER_H
#define KLEE_INTERPRETER_H

#include <vector>
#include <string>
#include <map>
#include <set>
#include <list>
#include "Replay.h"

struct KTest;

namespace llvm
{
class Function;
class Module;
}

namespace klee
{
class ExecutionState;
class Interpreter;
class TreeStreamWriter;

class InterpreterHandler
{
public:
	InterpreterHandler() {}
	virtual ~InterpreterHandler() {};

	virtual std::ostream &getInfoStream() const = 0;

	virtual std::string getOutputFilename(const std::string &filename) = 0;
	virtual std::ostream *openOutputFile(const std::string &filename) = 0;

	virtual unsigned getNumTestCases() const = 0;
	virtual unsigned getNumErrors(void) const = 0;
	virtual unsigned getNumPathsExplored() const = 0;
	virtual void incPathsExplored() = 0;
	virtual void incErrorsFound() = 0;

	virtual unsigned processTestCase(
		const ExecutionState &state,
		const char *err, const char *suffix) = 0;

	virtual bool isWriteOutput(void) const = 0;
	virtual void setWriteOutput(bool v) = 0;
};

class Interpreter
{
public:
  /// ModuleOptions - Module level options which can be set when
  /// registering a module with the interpreter.
  struct ModuleOptions {
    std::string LibraryDir;
    bool Optimize;
    bool CheckDivZero;
    std::vector<std::string> ExcludeCovFiles;

    /* XXX: this is stupid */
    ModuleOptions(const std::string& _LibraryDir, 
                  bool _Optimize, bool _CheckDivZero,
                  const std::vector<std::string> _ExcludeCovFiles)
      : LibraryDir(_LibraryDir), Optimize(_Optimize), 
        CheckDivZero(_CheckDivZero), ExcludeCovFiles(_ExcludeCovFiles) {}
  };

protected:
	Interpreter() {};

public:
	virtual ~Interpreter() {};

	/// Register the module to be executed.  
	///
	/// \return The final module after it has been optimized, checks
	/// inserted, and modified for interpretation.
	virtual const llvm::Module * 
	setModule(llvm::Module *module, const ModuleOptions &opts) = 0;

	// supply a tree stream writer which the interpreter will use
	// to record the symbolic path (as a stream of '0' and '1' bytes).
	virtual void setSymbolicPathWriter(TreeStreamWriter *tsw) = 0;

	// supply a test case to replay from. this can be used to drive the
	// interpretation down a user specified path. use null to reset.
	virtual void setReplayKTest(const struct KTest *out) = 0;

	virtual void setReplay(Replay* rp) = 0;

	virtual void runFunctionAsMain(
		llvm::Function *f,
		int argc,
		char **argv,
		char **envp) = 0;

	/*** Runtime options ***/
	virtual void setHaltExecution(bool value) = 0;
	virtual void setInhibitForking(bool value) = 0;

	/* State accessor methods ***/
	virtual unsigned getSymbolicPathStreamID(
		const ExecutionState &state) = 0;

	virtual bool getSymbolicSolution(
		const ExecutionState &state, 
		std::vector< 
		std::pair<
			std::string,
			std::vector<unsigned char> > >
		&res) = 0;

	virtual void getCoveredLines(
		const ExecutionState &state,
		std::map<const std::string*, std::set<unsigned> > &res) = 0;
};

}

#endif
