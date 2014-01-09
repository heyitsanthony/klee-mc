#include "SoftMMUHandlers.h"
#include "Executor.h"
#include "klee/Internal/Module/KModule.h"
#include <llvm/Support/Path.h>
#include <iostream>
#include <fstream>

using namespace klee;
namespace klee { extern llvm::Module* getBitcodeModule(const char* path); }

bool SoftMMUHandlers::isLoaded = false;

struct loadent
{
	const char*	le_name;
	KFunction**	le_kf;
	bool		le_required;
};

SoftMMUHandlers::SoftMMUHandlers(Executor& exe, const std::string& suffix)
{
	if (!isLoaded) {
		llvm::sys::Path path(exe.getKModule()->getLibraryDir());
		llvm::Module	*mod;

		path.appendComponent("libkleeRuntimeMMU.bc");
		mod = getBitcodeModule(path.c_str());
		assert (mod != NULL);

		exe.addModule(mod);
		std::cerr << "[SoftMMUHandlers] Loaded "<<path.c_str()<<'\n';
		isLoaded = true;
	}

	if (suffix.find('.') == std::string::npos) {
		loadBySuffix(exe, suffix);
	} else {
		loadByFile(exe, suffix);
	}
}

void SoftMMUHandlers::loadByFile(Executor& exe, const std::string& fname)
{
	std::vector<std::string>	suffixes;
	std::ifstream			ifs(fname.c_str());
	std::string			s;

	while (ifs >> s) {
		if (s.empty()) continue;
		suffixes.push_back(s);
	}

	assert (0 == 1 && "STUB");
}

void SoftMMUHandlers::loadBySuffix(Executor& exe, const std::string& suffix)
{
	KModule		*km(exe.getKModule());

	struct loadent	loadtab[] =  {
		{ "mmu_load_8", &f_load[0], true},
		{ "mmu_load_16", &f_load[1], true},
		{ "mmu_load_32", &f_load[2], true},
		{ "mmu_load_64", &f_load[3], true},
		{ "mmu_load_128", &f_load[4], true},
		{ "mmu_store_8", &f_store[0], true},
		{ "mmu_store_16", &f_store[1], true},
		{ "mmu_store_32", &f_store[2], true},
		{ "mmu_store_64", &f_store[3], true},
		{ "mmu_store_128", &f_store[4], true},
		{ "mmu_cleanup", &f_cleanup, false},
		{ "mmu_init", &f_init, false},
		{ "mmu_signal", &f_signal, false},
		{ NULL, NULL, false}};

	for (struct loadent* le = &loadtab[0]; le->le_name; le++) {
		std::string	func_name(le->le_name + ("_" + suffix));
		KFunction	*kf(km->getKFunction(func_name.c_str()));

		*(le->le_kf) = kf;
		if (kf != NULL || le->le_required == false) continue;
		std::cerr << "[SoftMMUHandlers] Not found: "<< func_name <<'\n';
	}
}
