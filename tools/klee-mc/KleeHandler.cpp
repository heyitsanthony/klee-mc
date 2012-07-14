#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "llvm/Support/Signals.h"
#include "llvm/Support/CommandLine.h"

#include "klee/Common.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/System/Time.h"
#include "klee/Internal/ADT/zfstream.h"
#include "klee/Solver.h"
#include "../../lib/Solver/SMTPrinter.h"
#include "static/Sugar.h"
#include "cmdargs.h"

#include "ExecutorVex.h"
#include "ExeStateVex.h"
#include "KleeHandler.h"
#include "guestsnapshot.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <cerrno>
#include <iostream>
#include <fstream>

using namespace llvm;
using namespace klee;

extern bool WriteTraces;

#define DECL_OPT(x,y,z)	cl::opt<bool> x(y,cl::desc(z))

namespace {
DECL_OPT(NoOutput, "no-output", "Don't generate test files");
DECL_OPT(WriteCov, "write-cov", "Write coverage info for each test");
DECL_OPT(WritePCs, "write-pcs", "Write .pc files for each test case");
DECL_OPT(WriteSMT,  "write-smt", "Write .smt for each test");
DECL_OPT(WritePaths, "write-paths", "Write .path files for each test");
DECL_OPT(WriteSymPaths, "write-sym-paths", "Write .sym.path files for each test");
DECL_OPT(ExitOnError, "exit-on-error", "Exit if errors occur");
DECL_OPT(ValidateTestCase, "validate-test", "Validate tests with concrete replay");

cl::opt<unsigned> StopAfterNTests(
	"stop-after-n-tests",
	cl::desc("Halt execution after generating the given number of tests."),
	cl::init(0));

cl::opt<std::string> OutputDir(
	"output-dir",
	cl::desc("Directory to write results in (defaults to klee-out-N)"),
	cl::init(""));
}

KleeHandler::KleeHandler(const CmdArgs* in_args, Guest* _gs)
: m_symPathWriter(0)
, m_infoFile(0)
, m_testIndex(0)
, m_pathsExplored(0)
, cmdargs(in_args)
, gs(_gs)
, m_interpreter(0)
{
	std::string theDir;

	theDir = setupOutputDir();
	std::cerr << "KLEE: output directory = \"" << theDir << "\"\n";

	sys::Path p(theDir);
	if (!sys::path::is_absolute(p.str())) {
		sys::Path cwd = sys::Path::GetCurrentDirectory();
		cwd.appendComponent(theDir);
		p = cwd;
	}

	strcpy(m_outputDirectory, p.c_str());

	if (mkdir(m_outputDirectory, 0775) < 0) {
		std::cerr <<
			"KLEE: ERROR: Unable to make output directory: \"" <<
			m_outputDirectory  <<
			"\", refusing to overwrite.\n";
		exit(1);
	}

	setupOutputFiles();

	if (ValidateTestCase) {
		assert (gs != NULL);
		assert (dynamic_cast<GuestSnapshot*>(gs) != NULL);
	}
}

bool KleeHandler::scanForOutputDir(const std::string& p, std::string& theDir)
{
	llvm::sys::Path	directory(p);
	int		err;

	std::string dirname = "";
	directory.eraseComponent();
	if (directory.isEmpty()) directory.set(".");

	for (int i = 0; ; i++) {
		char buf[256], tmp[64];
		sprintf(tmp, "klee-out-%d", i);
		dirname = tmp;
		sprintf(buf, "%s/%s", directory.c_str(), tmp);
		theDir = buf;

		if (DIR *dir = opendir(theDir.c_str())) {
			closedir(dir);
		} else {
			break;
		}
	}

	llvm::sys::Path klee_last(directory);
	klee_last.appendComponent("klee-last");

	err = unlink(klee_last.c_str());
	if (err < 0 && (errno != ENOENT))
		return false;

	if (symlink(dirname.c_str(), klee_last.c_str()) < 0)
		return false;

	return true;
}

std::string KleeHandler::setupOutputDir(void)
{
	std::string	theDir;

	if (OutputDir != "") return OutputDir;
	if (scanForOutputDir(cmdargs->getBinaryPath(), theDir)) return theDir;
	if (scanForOutputDir("", theDir)) return theDir;

	assert (0 == 1 && "failed to grab output dir");
	return "";
}

void KleeHandler::setupOutputFiles(void)
{
	char fname[1024];
	snprintf(fname, sizeof(fname), "%s/warnings.txt", m_outputDirectory);
	klee_warning_file = fopen(fname, "w");
	assert(klee_warning_file);

	snprintf(fname, sizeof(fname), "%s/messages.txt", m_outputDirectory);
	klee_message_file = fopen(fname, "w");
	assert(klee_message_file);

	m_infoFile = openOutputFile("info");
}

KleeHandler::~KleeHandler()
{
	if (m_symPathWriter) delete m_symPathWriter;
	delete m_infoFile;

	fclose(klee_message_file);
	fclose(klee_warning_file);
}

void KleeHandler::setInterpreter(Interpreter *i)
{
	m_interpreter = dynamic_cast<ExecutorVex*>(i);
	assert (m_interpreter != NULL && "Expected ExecutorVex interpreter");

	if (!WriteSymPaths) return;

	m_symPathWriter = new TreeStreamWriter(getOutputFilename("symPaths.ts"));
	assert(m_symPathWriter->good());
	m_interpreter->setSymbolicPathWriter(m_symPathWriter);
}

std::string KleeHandler::getOutputFilename(const std::string &filename)
{
	char outfile[1024];
	sprintf(outfile, "%s/%s", m_outputDirectory, filename.c_str());
	return outfile;
}

static std::ostream* file_check(std::ostream* os, const std::string& fname)
{
	if (os == NULL) {
		klee_error(
			"error opening file \"%s\" (out of memory)",
			fname.c_str());
		return NULL;
	}

	if (!os->good()) {
		klee_error(
			"error opening file \"%s\". KLEE may have run out of file "
			"descriptors: try to increase the max open file "
			"descriptors by using ulimit.", fname.c_str());
		delete os;
		return NULL;
	}

	return os;
}

#define IO_OUT_MODE	std::ios::out | std::ios::trunc | std::ios::binary

std::ostream *KleeHandler::openOutputFileGZ(const std::string &filename)
{
	std::ios::openmode io_mode = IO_OUT_MODE;
	std::string path = getOutputFilename(filename);
	std::ostream* f = new gzofstream(path.c_str(), io_mode);

	return file_check(f, filename);
}

std::ostream *KleeHandler::openOutputFile(const std::string &filename)
{
	std::ios::openmode io_mode = IO_OUT_MODE;
	std::string path = getOutputFilename(filename);
	std::ostream* f = new std::ofstream(path.c_str(), io_mode);
	return file_check(f, filename);
}

std::string KleeHandler::getTestFilename(const std::string &suffix, unsigned id)
{
	char filename[1024];
	sprintf(filename, "test%06d.%s", id, suffix.c_str());
	return getOutputFilename(filename);
}

std::ostream *KleeHandler::openTestFileGZ(
	const std::string &suffix, unsigned id)
{
	char filename[1024];
	sprintf(filename, "test%06d.%s.gz", id, suffix.c_str());
	return openOutputFileGZ(filename);
}

std::ostream *KleeHandler::openTestFile(const std::string &suffix, unsigned id)
{
	char filename[1024];
	sprintf(filename, "test%06d.%s", id, suffix.c_str());
	return openOutputFile(filename);
}

void KleeHandler::processSuccessfulTest(
	const char	*name,
	unsigned	id,
	out_objs&	out)
{
	KTest		b;
	bool		ktest_ok = false;

	b.numArgs = cmdargs->getArgc();
	b.args = cmdargs->getArgv();

	b.symArgvs = (cmdargs->isSymbolic())
		? cmdargs->getArgc()
		: 0;

	b.symArgvLen = 0;
	b.numObjects = out.size();
	b.objects = new KTestObject[b.numObjects];

	assert (b.objects);
	for (unsigned i=0; i<b.numObjects; i++) {
		KTestObject *o = &b.objects[i];
		o->name = const_cast<char*>(out[i].first.c_str());
		o->numBytes = out[i].second.size();
		o->bytes = new unsigned char[o->numBytes];
		assert(o->bytes);
		std::copy(out[i].second.begin(), out[i].second.end(), o->bytes);
	}

	errno = 0;

	std::ostream	*os = openTestFileGZ(name, id);
	if (os != NULL) {
		ktest_ok = kTest_toStream(&b, *os);
		delete os;
	}

	if (!ktest_ok) {
		klee_warning(
		"unable to write output test case, losing it (errno=%d: %s)",
		errno,
		strerror(errno));
	}

	for (unsigned i=0; i<b.numObjects; i++)
		delete[] b.objects[i].bytes;
	delete[] b.objects;
}

bool KleeHandler::getStateSymObjs(
	const ExecutionState& state,
	out_objs& out)
{ return m_interpreter->getSymbolicSolution(state, out); }

#define LOSING_IT(x)	\
	klee_warning("unable to write " #x " file, losing it (errno=%d: %s)", \
		errno, strerror(errno))

/* Outputs all files (.ktest, .pc, .cov etc.) describing a test case */
void KleeHandler::processTestCase(
	const ExecutionState &state,
	const char *errorMessage,
	const char *errorSuffix)
{
	unsigned	id;
	out_objs	out;
	bool		success;

	if (errorMessage && ExitOnError) {
		std::cerr << "EXITING ON ERROR:\n" << errorMessage << "\n";
		exit(1);
	}

	if (NoOutput) return;

	success = getStateSymObjs(state, out);
	if (!success)
		klee_warning("unable to get symbolic solution, losing test");

	id = ++m_testIndex;

	if (success) processSuccessfulTest("ktest", id, out);

	if (errorMessage) {
		if (std::ostream* f = openTestFile(errorSuffix, id)) {
			*f << errorMessage;
			delete f;
		} else
			LOSING_IT("error");
	}

	if (WritePaths) {
		if (std::ostream* f = openTestFileGZ("path", id)) {
			foreach(bit, state.branchesBegin(), state.branchesEnd()) {
				(*f) << (*bit).first
#ifdef INCLUDE_INSTR_ID_IN_PATH_INFO
				<< "," << (*bit).second
#endif
				<< "\n";
			}
			delete f;
		} else {
			LOSING_IT(".path");
		}
	}

	if (WriteSMT) {
		Query	query(	state.constraints,
				ConstantExpr::alloc(0, Expr::Bool));
		std::ostream	*f;

		f = openTestFileGZ("smt", id);
		SMTPrinter::print(*f, query);
		delete f;
	}

	if (errorMessage || WritePCs)
		dumpPCs(state, id);

	if (m_symPathWriter) {
		std::vector<unsigned char> symbolicBranches;

		m_symPathWriter->readStream(
			m_interpreter->getSymbolicPathStreamID(state),
			symbolicBranches);
		if (std::ostream* f = openTestFile("sym.path", id)) {
			std::copy(
				symbolicBranches.begin(), symbolicBranches.end(),
				std::ostream_iterator<unsigned char>(*f, "\n"));
			delete f;
		} else
			LOSING_IT(".sym.path");
	}

	if (WriteCov) {
		std::map<const std::string*, std::set<unsigned> > cov;
		m_interpreter->getCoveredLines(state, cov);
		if (std::ostream* f = openTestFile("cov", id)) {
			foreach (it, cov.begin(), cov.end()) {
			foreach (it2, it->second.begin(), it->second.end())
				*f << *it->first << ":" << *it2 << "\n";
			}
			delete f;
		} else
			LOSING_IT(".cov");
	}

	if (WriteTraces) {
		if (std::ostream* f = openTestFile("trace", id)) {
			state.exeTraceMgr.printAllEvents(*f);
			delete f;
		} else
			LOSING_IT(".trace");
	}

	if (m_testIndex == StopAfterNTests)
		m_interpreter->setHaltExecution(true);

	dumpLog(state, "crumbs", id);

	fprintf(stderr, "===DONE WRITING TESTID=%d (es=%p)===\n", id, &state);
	if (ValidateTestCase) {
		if (validateTest(id))
			std::cerr << "VALIDATED TEST#" << id << '\n';
		else
			std::cerr << "FAILED TO VALIDATE TEST#" << id << '\n';
	}
}

void KleeHandler::dumpPCs(const ExecutionState& state, unsigned id)
{
	std::string constraints;
	std::ostream* f;

	state.getConstraintLog(constraints);
	f = openTestFileGZ("pc", id);
	if (f == NULL) {
		klee_warning(
			"unable to write .pc file, losing it (errno=%d: %s)",
			errno, strerror(errno));
		return;
	}

	*f << constraints;
	delete f;
}

void KleeHandler::dumpLog(
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

void KleeHandler::getPathFiles(
	std::string path, std::vector<std::string> &results)
{
	llvm::sys::Path p(path);
	std::set<llvm::sys::Path> contents;
	std::string error;

	if (p.getDirectoryContents(contents, &error)) {
		std::cerr << "ERROR: unable to read path directory: "
			<< path << ": " << error << "\n";
		exit(1);
	}

	foreach (it, contents.begin(), contents.end()) {
		std::string f = it->str();
		if (f.substr(f.size() - 5, f.size()) == ".path")
			results.push_back(f);
	}
}

// load a .path file
#define IFSMODE	std::ios::in | std::ios::binary
void KleeHandler::loadPathFile(std::string name, ReplayPathType &buffer)
{
	std::istream	*is;

	if (name.substr(name.size() - 3) == ".gz") {
		std::string new_name = name.substr(0, name.size() - 3);
		is = new gzifstream(name.c_str(), IFSMODE);
	} else
		is = new std::ifstream(name.c_str(), IFSMODE);

	if (is == NULL || !is->good()) {
		assert(0 && "unable to open path file");
		if (is) delete is;
		return;
	}

	while (is->good()) {
		unsigned value;
		*is >> value;
		if (!is->good()) break;
		is->get();
#ifdef INCLUDE_INSTR_ID_IN_PATH_INFO
		unsigned id;
		*is >> id;
		is->get();
		buffer.push_back(std::make_pair(value,id));
#else
		buffer.push_back(value);
#endif
	}

	delete is;
}

void KleeHandler::getOutFiles(
	std::string path, std::vector<std::string> &results)
{
	llvm::sys::Path			p(path);
	std::set<llvm::sys::Path>	contents;
	std::string			error;

	if (p.getDirectoryContents(contents, &error)) {
		std::cerr
			<< "ERROR: unable to read output directory: " << path
			<< ": " << error << "\n";
		exit(1);
	}

	foreach (it, contents.begin(), contents.end()) {
		std::string f = it->str();
		if (f.substr(f.size()-6,f.size()) == ".ktest")
			results.push_back(f);
	}
}

bool KleeHandler::validateTest(unsigned id)
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
		argv[2] = m_outputDirectory;
		argv[3] = static_cast<GuestSnapshot*>(gs)->getPath().c_str();
		argv[4] = NULL;

		execvp("kmc-replay", (char**)argv); 
		_exit(-1);
	}

	ret_pid = waitpid(child_pid, &status, 0);
	if (ret_pid != child_pid) {
		std::cerr << "VALDIATE: BAD WAIT\n";
		return false;
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
