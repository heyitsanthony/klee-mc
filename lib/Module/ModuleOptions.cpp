#include <stdlib.h>
#include "klee/Config/config.h"
#include "klee/Interpreter.h"

using namespace klee;

ModuleOptions::ModuleOptions(
	const std::string& _LibraryDir, 
	bool _Optimize, bool _CheckDivZero,
	const std::vector<std::string> _ExcludeCovFiles)
: LibraryDir(_LibraryDir)
, Optimize(_Optimize)
, CheckDivZero(_CheckDivZero)
, ExcludeCovFiles(_ExcludeCovFiles) {}

ModuleOptions::ModuleOptions(
	  bool _Optimize, bool _CheckDivZero,
	  const std::vector<std::string> _ExcludeCovFiles)
: Optimize(_Optimize)
, CheckDivZero(_CheckDivZero)
, ExcludeCovFiles(_ExcludeCovFiles)
{
	/* select klee library dir based on env / defaults */
	if (getenv("KLEELIBDIR") != NULL) {
		LibraryDir = getenv("KLEELIBDIR");
		return;
	}

	LibraryDir = (KLEE_DIR "/" RUNTIME_CONFIGURATION "/lib");
}
