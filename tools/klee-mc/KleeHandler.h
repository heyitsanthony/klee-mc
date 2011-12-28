#ifndef KLEEHANDLER_H
#define KLEEHANDLER_H

#include "ExeStateVex.h"
#include "klee/Internal/ADT/TreeStream.h"
#include "klee/Interpreter.h"

class CmdArgs;

namespace klee
{
class ExecutorVex;

class KleeHandler : public InterpreterHandler
{
private:
	TreeStreamWriter *m_symPathWriter;
	std::ostream *m_infoFile;

	char m_outputDirectory[1024];
	unsigned m_testIndex;  // number of tests written so far
	unsigned m_pathsExplored; // number of paths explored so far

	// used for writing .ktest files
	const CmdArgs*	cmdargs;

public:
	KleeHandler(const CmdArgs* cmdargs);
	virtual ~KleeHandler();

	std::ostream &getInfoStream() const { return *m_infoFile; }
	unsigned getNumTestCases() { return m_testIndex; }
	unsigned getNumPathsExplored() { return m_pathsExplored; }
	void incPathsExplored() { m_pathsExplored++; }

	void setInterpreter(Interpreter *i);

	void processTestCase(
		const ExecutionState  &state,
		const char *errorMessage,
		const char *errorSuffix);

	std::string getOutputFilename(const std::string &filename);
	std::ostream *openOutputFile(const std::string &filename);
	std::string getTestFilename(const std::string &suffix, unsigned id);
	std::ostream *openTestFile(const std::string &suffix, unsigned id);

	// load a .out file
	static void loadOutFile(
		std::string name, std::vector<unsigned char> &buffer);

	// load a .path file
	static void loadPathFile(std::string name, ReplayPathType &buffer);

	static void getPathFiles(
		std::string path, std::vector<std::string> &results);

	static void getOutFiles(
		std::string path, std::vector<std::string> &results);

protected:
typedef
	std::vector< std::pair<std::string, std::vector<unsigned char> > >
	out_objs;
	virtual void processSuccessfulTest(
		const char* name, unsigned id, out_objs&);

	virtual bool getStateSymObjs(
		const ExecutionState& state,
		out_objs& out);

	ExecutorVex	*m_interpreter;
private:

	void setupOutputFiles(void);
	std::string setupOutputDir(void);
	bool scanForOutputDir(const std::string& path, std::string& theDir);
	void dumpPCs(const ExecutionState& state, unsigned id);
	void dumpLog(
		const char* name,
		unsigned id,
		RecordLog::const_iterator begin, RecordLog::const_iterator end);
};

}
#endif
