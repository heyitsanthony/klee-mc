#ifndef KLEEHANDLER_H
#define KLEEHANDLER_H

#include "klee/Internal/ADT/TreeStream.h"
#include "klee/Interpreter.h"
#include "klee/Common.h"
#include "klee/Internal/ADT/TwoOStreams.h"
#include <sys/times.h>
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
	std::unique_ptr<TreeStreamWriter> m_symPathWriter;
	std::unique_ptr<std::ostream>	m_infoFile;

	char		m_outputDirectory[1024];
	unsigned	m_testIndex;  // # tests written
	unsigned	m_pathsExplored; // # paths explored
	unsigned	m_errorsFound;

	// used for writing .ktest files
	const CmdArgs			*cmdargs;

public:
	KleeHandler(const CmdArgs* cmdargs = NULL);
	virtual ~KleeHandler();

	std::ostream &getInfoStream() const override { return *m_infoFile; }

	std::string getOutputFilename(const std::string &filename) override;
	std::unique_ptr<std::ostream>
		openOutputFile(const std::string &filename) override;

	unsigned getNumTestCases() const override { return m_testIndex; }
	unsigned getNumPathsExplored() const override {return m_pathsExplored;}
	unsigned getNumErrors(void) const override { return m_errorsFound; }
	void incPathsExplored() override { m_pathsExplored++; }
	void incErrorsFound(void) override { m_errorsFound++; }

	unsigned processTestCase(
		const ExecutionState  &state,
		const char *errorMessage,
		const char *errorSuffix) override;



	const char* getOutputDir(void) const { return m_outputDirectory; }


	virtual void setInterpreter(Interpreter *i);

	std::unique_ptr<std::ostream>
		openOutputFileGZ(const std::string &filename);

	std::string getTestFilename(const std::string &suffix, unsigned id);
	std::unique_ptr<std::ostream> openTestFile(
		const std::string &suffix, unsigned id);
	std::unique_ptr<std::ostream> openTestFileGZ(
		const std::string &suffix, unsigned id);

	static void getPathFiles(
		std::string path, std::vector<std::string> &results);

	static void getKTests(
		const std::vector<std::string>& files,
		const std::vector<std::string>& dirs,
		std::vector<KTest*>& ktests);

	static void loadPathFiles(
		const std::vector<std::string>& pathfiles,
		std::list<ReplayPath>& replayPaths);

	void printInfoHeader(int argc, char* argv[]);
	void printInfoFooter(void);

typedef
	std::vector< std::pair<std::string, std::vector<unsigned char> > >
	out_objs;
	void processSuccessfulTest(const char* name, unsigned id, out_objs&);
	virtual void processSuccessfulTest(std::ostream* os, out_objs&);

protected:
	static void getKTestFiles(
		std::string path, std::vector<std::string> &results);


	virtual bool getStateSymObjs(
		const ExecutionState& state, out_objs& out);

	virtual void printErrorMessage(
		const ExecutionState  &state,
		const char *errorMessage,
		const char *errorSuffix,
		unsigned id);

	Interpreter	*m_interpreter;

	void writeMem(const ExecutionState& s, unsigned id);

	static unsigned getStopAfterNTests(void);
private:
	void printStats(PrefixWriter& info);
	void printCWEXML(
		const ExecutionState &state, const char* errorMessage);

	void setupOutputFiles(void);
	std::string setupOutputDir(void);
	bool scanForOutputDir(const std::string& path, std::string& theDir);
	void dumpPCs(const ExecutionState& state, unsigned id);

	time_t t[2];
	clock_t tm[2];
	struct tms tms[2];
	bool tms_valid;
};

}
#endif
