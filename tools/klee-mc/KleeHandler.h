#ifndef KLEEHANDLER_H
#define KLEEHANDLER_H

#include "klee/ExecutionState.h"
#include "klee/Internal/ADT/TreeStream.h"
#include "klee/Interpreter.h"

class CmdArgs;

namespace klee
{

class KleeHandler : public InterpreterHandler
{
private:
  Interpreter *m_interpreter;
  TreeStreamWriter *m_symPathWriter;
  std::ostream *m_infoFile;

  char m_outputDirectory[1024];
  unsigned m_testIndex;  // number of tests written so far
  unsigned m_pathsExplored; // number of paths explored so far

  // used for writing .ktest files
  const CmdArgs*	cmdargs;
public:
  KleeHandler(const CmdArgs* cmdargs);
  ~KleeHandler();

  std::ostream &getInfoStream() const { return *m_infoFile; }
  unsigned getNumTestCases() { return m_testIndex; }
  unsigned getNumPathsExplored() { return m_pathsExplored; }
  void incPathsExplored() { m_pathsExplored++; }

  void setInterpreter(Interpreter *i);

  void processTestCase(const ExecutionState  &state,
                       const char *errorMessage, 
                       const char *errorSuffix);

  std::string getOutputFilename(const std::string &filename);
  std::ostream *openOutputFile(const std::string &filename);
  std::string getTestFilename(const std::string &suffix, unsigned id);
  std::ostream *openTestFile(const std::string &suffix, unsigned id);

  // load a .out file
  static void loadOutFile(std::string name, 
                          std::vector<unsigned char> &buffer);

  // load a .path file
  static void loadPathFile(std::string name, Interpreter::ReplayPathType &buffer);

  static void getPathFiles(std::string path,
                           std::vector<std::string> &results);

  static void getOutFiles(std::string path,
			  std::vector<std::string> &results);
private:
	void setupOutputFiles(void);
	std::string setupOutputDir(void);
};

}
#endif
