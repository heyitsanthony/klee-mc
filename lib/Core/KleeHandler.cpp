#include <llvm/Support/Signals.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Path.h>

#include "klee/Common.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/System/Time.h"
#include "klee/Internal/ADT/zfstream.h"
#include "klee/Solver.h"
#include "../../lib/Solver/SMTPrinter.h"
#include "static/Sugar.h"
#include "klee/Internal/ADT/CmdArgs.h"
#include "klee/Statistics.h"

#include "klee/Internal/Module/KFunction.h"
#include "klee/KleeHandler.h"
#include "klee/ExecutionState.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#include <iostream>
#include <fstream>

using namespace llvm;
using namespace klee;

#define DECL_OPT(x,y,z)	cl::opt<bool> x(y,cl::desc(z))

namespace {
DECL_OPT(NoOutput, "no-output", "Don't generate test files");
DECL_OPT(WriteCov, "write-cov", "Write coverage info for each test");
DECL_OPT(WritePCs, "write-pcs", "Write .pc files for each test case");
DECL_OPT(WriteSMT,  "write-smt", "Write .smt for each test");
DECL_OPT(WritePaths, "write-paths", "Write .path files for each test");
DECL_OPT(WriteSymPaths, "write-sym-paths", "Write .sym.path files for each test");
DECL_OPT(WriteMem, "write-mem", "Write updated memory objects for each test");
DECL_OPT(ExitOnError, "exit-on-error", "Exit if errors occur");
DECL_OPT(ClobberOutput, "clobber-output", "Continue if output directory already exists");

cl::opt<unsigned> StopAfterNTests(
	"stop-after-n-tests",
	cl::desc("Halt execution after generating the given number of tests."));

cl::opt<unsigned> StopAfterNErrors(
	"stop-after-n-errors",
	cl::desc("Halt execution after generating the given number of errors."));

cl::opt<std::string> OutputDir(
	"output-dir",
	cl::desc("Directory to write results in (defaults to klee-out-N)"));

cl::opt<std::string> WriteCWE(
	"write-cwe",
	cl::desc("Output CWE XML for errors (input is test case name)"));
}

static CmdArgs	ca_dummy;

KleeHandler::KleeHandler(const CmdArgs* in_args)
: m_testIndex(0)
, m_pathsExplored(0)
, m_errorsFound(0)
, cmdargs(in_args)
, m_interpreter(0)
, tms_valid(false)
{
	std::string theDir;

	setWriteOutput(NoOutput == false);
	if (cmdargs == NULL) cmdargs = &ca_dummy;

	theDir = setupOutputDir();
	std::cerr << "KLEE: output directory = \"" << theDir << "\"\n";

	std::string p(theDir);
	if (!sys::path::is_absolute(p)) {
		char	cwd[PATH_MAX];
		getcwd(cwd, PATH_MAX);
		p = cwd + ("/" + theDir);
	}

	strcpy(m_outputDirectory, p.c_str());

	if (ClobberOutput) {
		struct stat s;
		int	err = stat(m_outputDirectory, &s);
		if (err != 0 || !S_ISDIR(s.st_mode)) {
			std::cerr
			<< "KLEE: ERROR: Unable to use output directory: \""
			<< m_outputDirectory 
			<< "\". Can't continue\n";
			exit(1);
		}
	} else if (mkdir(m_outputDirectory, 0775) < 0) {
		std::cerr <<
			"KLEE: ERROR: Unable to make output directory: \"" <<
			m_outputDirectory  <<
			"\", refusing to overwrite.\n";
		exit(1);
	}

	setupOutputFiles();
}

bool KleeHandler::scanForOutputDir(const std::string& p, std::string& theDir)
{
	std::string	directory(p), dirname;
	int		err;

	for (int i = directory.size() - 1; i; i--)
		if (directory[i] == '/') {
			directory = directory.substr(0, i);
			break;
		}
	if (directory.empty()) directory = ".";

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

	std::string klee_last(directory);
	klee_last = klee_last + "/klee-last";

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

	m_infoFile =openOutputFile("info");
}

KleeHandler::~KleeHandler()
{
	m_symPathWriter = nullptr;
	m_infoFile = nullptr;

	fclose(klee_message_file);
	fclose(klee_warning_file);
}

void KleeHandler::setInterpreter(Interpreter *i)
{
	m_interpreter = i;

	if (!WriteSymPaths) return;

	auto output_fname = getOutputFilename("symPaths.ts");
	m_symPathWriter = std::make_unique<TreeStreamWriter>(output_fname);
	assert(m_symPathWriter->good());
	m_interpreter->setSymbolicPathWriter(m_symPathWriter.get());
}

std::string KleeHandler::getOutputFilename(const std::string &filename)
{
	char outfile[1024];
	sprintf(outfile, "%s/%s", m_outputDirectory, filename.c_str());
	return outfile;
}

static std::unique_ptr<std::ostream> file_check(
	std::ostream* os, const std::string& fname)
{
	if (os == NULL) {
		klee_error(
			"error opening file \"%s\" (out of memory)",
			fname.c_str());
		return nullptr;
	}

	if (!os->good()) {
		klee_error(
			"error opening file \"%s\". KLEE may have run out of file "
			"descriptors: try to increase the max open file "
			"descriptors by using ulimit.", fname.c_str());
		delete os;
		return nullptr;
	}

	return std::unique_ptr<std::ostream>(os);
}

#define IO_OUT_MODE	std::ios::out | std::ios::trunc | std::ios::binary

std::unique_ptr<std::ostream> KleeHandler::openOutputFileGZ(
	const std::string &filename)
{
	std::ios::openmode io_mode = IO_OUT_MODE;
	std::string path = getOutputFilename(filename);
	std::ostream* f = new gzofstream(path.c_str(), io_mode);
	return file_check(f, filename);
}

std::unique_ptr<std::ostream> KleeHandler::openOutputFile(
	const std::string &filename)
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

std::unique_ptr<std::ostream> KleeHandler::openTestFileGZ(
	const std::string &suffix, unsigned id)
{
	char filename[1024];
	sprintf(filename, "test%06d.%s.gz", id, suffix.c_str());
	return openOutputFileGZ(filename);
}

std::unique_ptr<std::ostream> KleeHandler::openTestFile(
	const std::string &suffix, unsigned id)
{
	char filename[1024];
	sprintf(filename, "test%06d.%s", id, suffix.c_str());
	return openOutputFile(filename);
}

void KleeHandler::processSuccessfulTest(std::ostream* os, out_objs& out)
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
	if (os != NULL)
		ktest_ok = kTest_toStream(&b, *os);

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

void KleeHandler::processSuccessfulTest(
	const char *name, unsigned id, out_objs &out)
{
	processSuccessfulTest(openTestFileGZ(name, id).get(), out);
}

bool KleeHandler::getStateSymObjs(
	const ExecutionState& state,
	out_objs& out)
{ return m_interpreter->getSymbolicSolution(state, out); }

/* in Basic/CWE.cpp */
namespace klee { extern int CWE_xlate(const char* s); }

void KleeHandler::printCWEXML(
	const ExecutionState &state,
	const char* errorMessage)
{
	auto		f = openTestFile("xml", m_testIndex - 1);
	int		cweID;

	if (f == NULL)
		return;

	cweID = CWE_xlate(errorMessage);

	(*f)	<< "<structured_message>\n"
		<< " <message_type>found_cwe</message_type>\n"
		<< " <test_case>" << WriteCWE << "</test_case>\n"
		<< " <cwe_entry_id>" << cweID << "</cwe_entry_id>\n"
		<< " <filename>?</filename>\n"
		<< " <method>"
		/* XXX: this is probably bad */
		<< state.prevPC->getInst()->getParent()->getParent()->getName().str()
		<< "</method>\n"
		<< " <line_number>?</line_number>\n"
		<< "</structured_message>\n";

	(*f)	<< "\n<structured_message>\n"
		<< " <message_type>controlled_exit</message_type>\n"
		<< " <test_case>" << WriteCWE << "</test_case>\n"
		<< "</structured_message>\n";
}


#if 0
void KleeHandler::updateTestCoverage(const ExecutionState& es)
{
	/* scan kfuncs that have uncovered instructions;
	 * if ... */
	assert (0 == 1 && "STUB");
}
#endif

/* Outputs all files (.ktest, .pc, .cov etc.) describing a test case */
unsigned KleeHandler::processTestCase(
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

	if (!isWriteOutput()) return 0;

	/* TODO: scan path to figure out what was covered... */
	// updateTestCoverage(state);

	success = getStateSymObjs(state, out);
	if (!success)
		klee_warning("unable to get symbolic solution, losing test");

	id = ++m_testIndex;

	if (success) processSuccessfulTest("ktest", id, out);

	if (errorMessage != NULL)
		printErrorMessage(state, errorMessage, errorSuffix, id);

	if (errorMessage != NULL && !WriteCWE.empty()) {
		printCWEXML(state, errorMessage);
	}

	if (WritePaths) {
		const char	*fprefix;
		fprefix = (state.concretizeCount || state.isPartial)
			? "pathpart" : "path";
		auto f = openTestFileGZ(fprefix, id);
		if (f) {
			Replay::writePathFile(
				(const Executor&)*m_interpreter, state, *f);
		} else {
			LOSING_IT(".path");
		}
	}

	if (WriteSMT) {
		Query	query(state.constraints, MK_CONST(0, Expr::Bool));
		auto f = openTestFileGZ("smt", id);
		if (f) SMTPrinter::print(*f, query);
	}

	if (state.getNumMinInstKFuncs()) {
		auto f = openTestFileGZ("mininst", id);
		if (f) state.printMinInstKFunc(*f);
	}

	if (errorMessage || WritePCs)
		dumpPCs(state, id);

	if (m_symPathWriter) {
		std::vector<unsigned char> symbolicBranches;

		m_symPathWriter->readStream(
			m_interpreter->getSymbolicPathStreamID(state),
			symbolicBranches);
		auto f = openTestFile("sym.path", id);
		if (f) {
			std::copy(
				symbolicBranches.begin(), symbolicBranches.end(),
				std::ostream_iterator<unsigned char>(*f, "\n"));
		} else
			LOSING_IT(".sym.path");
	}

	if (WriteCov) {
		std::map<const std::string*, std::set<unsigned> > cov;
		m_interpreter->getCoveredLines(state, cov);
		auto f = openTestFile("cov", id);
		if (f) {
			foreach (it, cov.begin(), cov.end()) {
			foreach (it2, it->second.begin(), it->second.end())
				*f << *it->first << ":" << *it2 << "\n";
			}
		} else
			LOSING_IT(".cov");
	}

	if (WriteMem) writeMem(state, id);

	if (m_testIndex == StopAfterNTests)
		m_interpreter->setHaltExecution(true);

	if (StopAfterNErrors && m_errorsFound >= StopAfterNErrors) {
		std::cerr << "[KLEE] Stopping after "
			<< m_errorsFound << " errors.\n";
		m_interpreter->setHaltExecution(true);
	}

	return id;
}

void KleeHandler::writeMem(const ExecutionState& state, unsigned id)
{
	char	dname[PATH_MAX];
	
	sprintf(dname, "%s/mem%06d", m_outputDirectory, id);
	mkdir(dname, 0755);

	foreach (it, state.addressSpace.begin(), state.addressSpace.end()) {
		char			fname[PATH_MAX];
		const MemoryObject	*mo(it->first);
		const ObjectState	*os(state.addressSpace.findObject(mo));
		void			*base;
		FILE			*f;

		base = (void*)mo->address;

		/* ignore unmodified objects */
		if (os->isZeroPage()) continue;
		if (os->readOnly) continue;
		if (!os->hasOwner()) continue;
		if (os->getCopyDepth() == 0) continue;

		sprintf(fname, "%s/%p.dat", dname, base);
		f = fopen(fname, "wb");
		fwrite(os->getConcreteBuf(), mo->size, 1, f);
		fclose(f);

		sprintf(fname, "%s/%p.mask", dname, base);
		f = fopen(fname, "wb");
		for (unsigned int i = 0; i < mo->size; i++) {
			char	c(os->isByteConcrete(i) ? 0xff : 0);
			fwrite(&c, 1, 1, f);
		}
		fclose(f);
	}
}

void KleeHandler::dumpPCs(const ExecutionState& state, unsigned id)
{
	std::string constraints;

	state.getConstraintLog(constraints);
	auto f = openTestFileGZ("pc", id);
	if (f == NULL) {
		klee_warning(
			"unable to write .pc file, losing it (errno=%d: %s)",
			errno, strerror(errno));
		return;
	}
	*f << constraints;
}

static void loadContents(
	const std::string& path,
	std::set<std::string>&	contents)
{
	DIR			*d;

	d = opendir(path.c_str());
	if (d == NULL) {
		std::cerr << "Error reading " << path << '\n';
		exit(1);
	}

	while (struct dirent* de = readdir(d))
		contents.insert(path + "/" + std::string(de->d_name));

	closedir(d);
}


void KleeHandler::getPathFiles(
	std::string path, std::vector<std::string> &results)
{
	std::set<std::string>	contents;

	loadContents(path, contents);

	foreach (it, contents.begin(), contents.end()) {
		std::string f(*it);
		if (f.size() < 5) continue;
		if (	(f.substr(f.size() - 5, f.size()) == ".path") ||
			(f.substr(f.size() - 8, f.size()) == ".path.gz"))
		{
			results.push_back(f);
		}
	}
}


void KleeHandler::getKTests(
	const std::vector<std::string>& named,
	const std::vector<std::string>& dirs,
	std::vector<KTest*>& ktests)
{
	std::vector<std::string>	ktest_fnames(named);

	foreach (it, dirs.begin(), dirs.end())
		getKTestFiles(*it, ktest_fnames);

	foreach (it, ktest_fnames.begin(), ktest_fnames.end()) {
		KTest *out = kTest_fromFile(it->c_str());
		if (out != NULL) {
			ktests.push_back(out);
		} else {
			std::cerr << "KLEE: unable to open: " << *it << "\n";
		}
	}
}

void KleeHandler::getKTestFiles(
	std::string path, std::vector<std::string> &results)
{
	std::set<std::string>	contents;

	loadContents(path, contents);

	foreach (it, contents.begin(), contents.end()) {
		std::string f(*it);
		if (f.size() < 9) continue;
		if (	f.substr(f.size()-6,f.size()) == ".ktest" || 
			f.substr(f.size()-9,f.size()) == ".ktest.gz")
		{
			results.push_back(f);
		}
	}
}

void KleeHandler::printErrorMessage(
	const ExecutionState& state,
	const char* errorMessage,
	const char* errorSuffix,
	unsigned id)
{
	auto f = openTestFile(errorSuffix, id);
	if (f)
		*f << errorMessage;
	else
		LOSING_IT("error");
}

// returns the end of the string put in buf
static char *format_tdiff(char *buf, long seconds)
{
	assert(seconds >= 0);

	long minutes = seconds / 60;  seconds %= 60;
	long hours   = minutes / 60;  minutes %= 60;
	long days    = hours   / 24;  hours   %= 24;

	if (days > 0) buf += sprintf(buf, "%ld days, ", days);
	buf += sprintf(buf, "%02ld:%02ld:%02ld", hours, minutes, seconds);
	return buf;
}

static char *format_tdiff(char *buf, double seconds)
{
    long int_seconds = static_cast<long> (seconds);
    buf = format_tdiff(buf, int_seconds);
    buf += sprintf(buf, ".%02d", static_cast<int> (100 * (seconds - int_seconds)));
    return buf;
}



unsigned KleeHandler::getStopAfterNTests(void) { return StopAfterNTests; }

void KleeHandler::loadPathFiles(
	const std::vector<std::string>& pathFiles,
	std::list<ReplayPath>& replayPaths)
{
	ReplayPath	replayPath;

	foreach (it, pathFiles.begin(), pathFiles.end()) {
		Replay::loadPathFile(*it, replayPath);
		replayPaths.push_back(replayPath);
		replayPath.clear();
	}
}

static void printTimes(PrefixWriter& info, struct tms* tms, clock_t* tm, time_t* t)
{
	char buf[256], *pbuf;
	bool tms_valid = true;

	t[1] = time(NULL);
	tm[1] = times(&tms[1]);
	if (tm[1] == (clock_t) -1) {
		perror("times");
		tms_valid = false;
	}

	strftime(
		buf, sizeof(buf),
		"Finished: %Y-%m-%d %H:%M:%S\n", localtime(&t[1]));
	info << buf;

	pbuf = buf;
	pbuf += sprintf(buf, "Elapsed: ");

#define FMT_TDIFF(x,y) format_tdiff(pbuf, (x - y) / (double)clk_tck)
	if (tms_valid) {
		const long clk_tck = sysconf(_SC_CLK_TCK);
		pbuf = FMT_TDIFF(tm[1], tm[0]);
		pbuf += sprintf(pbuf, " (user ");
		pbuf = FMT_TDIFF(tms[1].tms_utime, tms[0].tms_utime);
		*pbuf++ = '+';
		pbuf = FMT_TDIFF(tms[1].tms_cutime, tms[0].tms_cutime);
		pbuf += sprintf(pbuf, ", sys ");
		pbuf = FMT_TDIFF(tms[1].tms_stime, tms[0].tms_stime);
		*pbuf++ = '+';
		pbuf = FMT_TDIFF(tms[1].tms_cstime, tms[0].tms_cstime);
		pbuf += sprintf(pbuf, ")");
	} else
		pbuf = format_tdiff(pbuf, t[1] - t[0]);

	strcpy(pbuf, "\n");
	info << buf;
}

#define GET_STAT(x,y)  \
	uint64_t x = *theStatisticManager->getStatisticByName(y);

void KleeHandler::printStats(PrefixWriter& info)
{
	GET_STAT(queries, "Queries")
	GET_STAT(queriesValid, "QueriesValid")
	GET_STAT(queriesInvalid, "QueriesInvalid")
	GET_STAT(queryCounterexamples, "QueriesCEX")
	GET_STAT(queriesFailed, "QueriesFailed")
	GET_STAT(queryConstructs, "QueriesConstructs")
	GET_STAT(queryCacheHits, "QueryCacheHits")
	GET_STAT(queryCacheMisses, "QueryCacheMisses")
	GET_STAT(instructions, "Instructions")
	GET_STAT(forks, "Forks")

	info	<< "done: total queries = " << queries << " ("
		<< "valid: " << queriesValid << ", "
		<< "invalid: " << queriesInvalid << ", "
		<< "failed: " << queriesFailed << ", "
		<< "cex: " << queryCounterexamples << ")\n";

	if (queries)
		info	<< "done: avg. constructs per query = "
		 	<< queryConstructs / queries << "\n";

	info	<< "done: query cache hits = " << queryCacheHits << ", "
		<< "query cache misses = " << queryCacheMisses << "\n";

	info << "done: total instructions = " << instructions << "\n";
	info << "done: explored paths = " << 1 + forks << "\n";
	info << "done: completed paths = " << getNumPathsExplored() << "\n";
	info << "done: generated tests = " << getNumTestCases() << "\n";
}

void KleeHandler::printInfoFooter(void)
{
	TwoOStreams info2s(&std::cerr, m_infoFile.get());
	PrefixWriter info(info2s, "KLEE: ");

	printTimes(info, tms, tm, t);
	printStats(info);
}

void KleeHandler::printInfoHeader(int argc, char* argv[])
{
	char buf[256];

	for (int i=0; i<argc; i++) {
		(*m_infoFile) << argv[i] << (i+1<argc ? " ":"\n");
	}

	TwoOStreams info2s(&std::cerr, m_infoFile.get());
	PrefixWriter info(info2s, "KLEE: ");
	info << "PID: " << getpid() << "\n";

	t[0] = time(NULL);
	tm[0] = times(&tms[0]);
	if (tm[0] == (clock_t) -1) {
		perror("times");
		tms_valid = false;
	}
	strftime(
		buf,
		sizeof(buf),
		"Started: %Y-%m-%d %H:%M:%S\n",
		localtime(&t[0]));
	info << buf;

	m_infoFile->flush();
}


