#ifndef KLEEHANDLER_H
#define KLEEHANDLER_H

#include "klee/Internal/ADT/TreeStream.h"
#include "klee/Interpreter.h"
#include "klee/Common.h"
#include "klee/Internal/ADT/TwoOStreams.h"
#include <cerrno>

#define LOSING_IT(x)	\
	klee_warning("unable to write " #x " file, losing it (errno=%d: %s)", \
		errno, strerror(errno))

namespace klee
{
class CmdArgs;

class KleeHandler : public InterpreterHandler
{
private:
	TreeStreamWriter		*m_symPathWriter;
	std::ostream			*m_infoFile;

	char				m_outputDirectory[1024];
	unsigned			m_testIndex;  // # tests written
	unsigned			m_pathsExplored; // # paths explored
	unsigned			m_errorsFound;

	// used for writing .ktest files
	const CmdArgs			*cmdargs;
	bool				writeOutput;

public:
	KleeHandler(const CmdArgs* cmdargs);
	virtual ~KleeHandler();

	bool isWriteOutput(void) const { return writeOutput; }
	void setWriteOutput(bool v) { writeOutput = v; }

	const char* getOutputDir(void) const { return m_outputDirectory; }

	std::ostream &getInfoStream() const { return *m_infoFile; }
	unsigned getNumTestCases() const { return m_testIndex; }
	unsigned getNumPathsExplored() const { return m_pathsExplored; }
	unsigned getNumErrors(void) const { return m_errorsFound; }
	void incPathsExplored() { m_pathsExplored++; }
	void incErrorsFound(void) { m_errorsFound++; }

	virtual void setInterpreter(Interpreter *i);

	virtual unsigned processTestCase(
		const ExecutionState  &state,
		const char *errorMessage,
		const char *errorSuffix);

	std::string getOutputFilename(const std::string &filename);
	std::ostream *openOutputFile(const std::string &filename);
	std::ostream *openOutputFileGZ(const std::string &filename);

	std::string getTestFilename(const std::string &suffix, unsigned id);
	std::ostream *openTestFile(const std::string &suffix, unsigned id);
	std::ostream *openTestFileGZ(const std::string &suffix, unsigned id);

	// load a .out file
	static void loadOutFile(
		std::string name, std::vector<unsigned char> &buffer);

	static void getPathFiles(
		std::string path, std::vector<std::string> &results);

	static void getKTests(
		const std::vector<std::string>& files,
		const std::vector<std::string>& dirs,
		std::vector<KTest*>& ktests);


	static void loadPathFiles(
		const std::vector<std::string>& pathfiles,
		std::list<ReplayPath>& replayPaths);


	void printStats(PrefixWriter& info);
protected:
	static void getKTestFiles(
		std::string path, std::vector<std::string> &results);


typedef
	std::vector< std::pair<std::string, std::vector<unsigned char> > >
	out_objs;
	virtual void processSuccessfulTest(
		const char* name, unsigned id, out_objs&);

	virtual bool getStateSymObjs(
		const ExecutionState& state, out_objs& out);

	virtual void printErrorMessage(
		const ExecutionState  &state,
		const char *errorMessage,
		const char *errorSuffix,
		unsigned id);

	Interpreter	*m_interpreter;

	static unsigned getStopAfterNTests(void);
private:
	void printCWEXML(
		const ExecutionState &state, const char* errorMessage);

	void setupOutputFiles(void);
	std::string setupOutputDir(void);
	bool scanForOutputDir(const std::string& path, std::string& theDir);
	void dumpPCs(const ExecutionState& state, unsigned id);
};

}
#endif
