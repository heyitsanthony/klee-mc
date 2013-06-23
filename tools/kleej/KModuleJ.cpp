#include <llvm/IR/Function.h>
#include <llvm/PassManager.h>
#include "KModuleJ.h"
#include "JnJavaName.h"
#include "JnIntrinsicPass.h"
#include "static/Sugar.h"
#include <sstream>

using namespace klee;


char JnIntrinsicPass::ID = 0;

KModuleJ::~KModuleJ(void) 
{
	foreach (it, f2jjn.begin(), f2jjn.end())
		delete it->second;
}

KFunction* KModuleJ::addFunction(llvm::Function* f)
{
	JnJavaName	*jjn;

	if (f->isDeclaration()) return NULL;

	jjn = JnJavaName::create(f->getName().str());
	if (jjn != NULL) {
		std::stringstream	ss;
		f2jjn.insert(std::make_pair(f, jjn));
		jjn->print(ss);
		setPrettyName(f, ss.str());
	}

	return KModule::addFunction(f);
}


void KModuleJ::prepare(InterpreterHandler *ihandler)
{
	llvm::FunctionPassManager	_fpm(module);

	_fpm.add(new JnIntrinsicPass(module));
	foreach (it, module->begin(), module->end())
		_fpm.run(*it);

	addFunctionPass(new JnIntrinsicPass(module));
	KModule::prepare(ihandler);
}


const char* af_tab_fns[] =
{
"onCreate",
NULL
};

const char* ac_ign_paths[] =
{
"android.app.Activity",
NULL
};

std::set<std::string> af_tab;
std::set<std::string> ac_ign_tab;

static void init_af_tab(void)
{
	if (af_tab.empty() == false)
		return;
	for (unsigned i = 0; af_tab_fns[i]; i++)
		af_tab.insert(af_tab_fns[i]);
	for (unsigned i = 0; ac_ign_paths[i]; i++)
		ac_ign_tab.insert(ac_ign_paths[i]);
}

KFunction* KModuleJ::getEntryFunction(void) const
{
	const JnJavaName	*ret_jjn;
	KFunction		*ret = NULL;

	init_af_tab();

	foreach (it, f2jjn.begin(), f2jjn.end()) {
		const JnJavaName	*jjn(it->second);

		if (	!af_tab.count(jjn->getMethod()) ||
			ac_ign_tab.count(jjn->getPath()))
			continue;

		if (ret != NULL) {
			std::cerr << "[KModuleJ] Multiple entries??\n";
			ret_jjn->print(std::cerr);
			std::cerr << '\n';
			jjn->print(std::cerr);
			std::cerr << '\n';
			return NULL;
		}

		ret = getKFunction(it->first);
		ret_jjn = jjn;
	}

	return ret;
}
