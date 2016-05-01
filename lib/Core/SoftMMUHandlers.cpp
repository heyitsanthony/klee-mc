#include "SoftMMUHandlers.h"
#include "Executor.h"
#include "Globals.h"
#include "klee/Internal/Module/KModule.h"
#include <llvm/IR/Module.h>
#include "klee/Internal/Support/ModuleUtil.h"
#include <iostream>
#include <fstream>

using namespace klee;

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
		std::string	path(exe.getKModule()->getLibraryDir());

		path = path + ("/libkleeRuntimeMMU.bc");

		auto mod = getBitcodeModule(path.c_str());
		assert (mod != NULL);
		exe.addModule(std::move(mod));

		std::cerr << "[SoftMMUHandlers] Loaded "<< path <<'\n';
		isLoaded = true;
	}

	if (suffix.find('.') == std::string::npos)
		loadBySuffix(exe, suffix);
	else
		loadByFile(exe, suffix);
}

void SoftMMUHandlers::loadByFile(Executor& exe, const std::string& fname)
{
	std::vector<std::string>	suffixes;
	std::ifstream			ifs(fname.c_str());
	std::string			s;

	while (ifs >> s) if (!s.empty()) suffixes.push_back(s);
	assert (!suffixes.empty());

	for (unsigned i = 1; i < suffixes.size(); i++) {
		std::string	prev("mmu_ops_" + suffixes[i-1]),
				cur("mmu_ops_" + suffixes[i]);
		MemoryObject	*mo(exe.getGlobals()->findObject(prev.c_str()));
		std::cerr << "[SoftMMUHandlers] " << prev << "->" << cur << '\n';
		assert (mo);
		exe.getCurrentState()->addressSpace.findWriteableObject(mo)->
			write(0, exe.getGlobals()->findAddress(cur.c_str()));
	}

	loadBySuffix(exe, suffixes[0]);
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
