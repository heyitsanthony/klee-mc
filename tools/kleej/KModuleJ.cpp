#include <llvm/IR/Function.h>
#include <llvm/PassManager.h>
#include <llvm/Linker/Linker.h>
#include "KModuleJ.h"
#include "JnJavaName.h"
#include "JnIntrinsicPass.h"
#include "static/Sugar.h"
#include <sstream>
#include <dirent.h>
#include <string.h>

using namespace llvm;
using namespace klee;


char JnIntrinsicPass::ID = 0;

KModuleJ::~KModuleJ(void) 
{
	foreach (it, f2jjn.begin(), f2jjn.end())
		delete it->second;
}

KFunction* KModuleJ::addFunction(Function* f)
{
	std::stringstream	ss;
	std::string		fn_name;
	JnJavaName		*jjn;

	fn_name = f->getName().str();

	jjn = JnJavaName::create(fn_name);
	if (jjn == NULL)
		return KModule::addFunction(f);

	if (f->hasExternalWeakLinkage()) {
		delete jjn;
		return getKFunction(fn_name.c_str());
	}

	if (f->isDeclaration()) {
		delete jjn;
		return NULL;
	}

	f2jjn.insert(std::make_pair(f, jjn));
	jjn->print(ss);
	setPrettyName(f, ss.str());
	std::cerr << "adding: ";
	jjn->print(std::cerr);
	std::cerr << '\n';

	return KModule::addFunction(f);
}


//static const char classPath[] = "/opt/klee/jclasses/android-17";
static const char classPath[] = ".";

namespace klee { extern Module* getBitcodeModule(const char* p); }

static bool isSafeDup(const std::string& s)
{
	if (s.find("__") != 0)
		return false;

	return (s.find("virtual_buf") != std::string::npos ||
		s.find("static_buf") != std::string::npos);
}



strset_t KModuleJ::findFuncClassPaths(void)
{
	strset_t	ss;

	foreach (it, module->begin(), module->end()) {
		JnJavaName	*jjn;
		if (it->hasExternalWeakLinkage() == false)
			continue;

		jjn = JnJavaName::create(it->getName().str());
		if (jjn == NULL)
			continue;

		strset_t	t(findJJNClassPaths(*jjn));
		ss.insert(t.begin(), t.end());

		delete jjn;
	}

	return ss;

}

strset_t KModuleJ::findAnonClassPaths(const JnJavaName& jjn)
{
	std::string			path;
	std::string			bcpath;
	DIR				*dir;
	strset_t			v;
	
	path = std::string(classPath) + "/";

	dir = opendir((path + jjn.getDir()).c_str());
	if (dir == NULL)
		return v;

	while (struct dirent* de = readdir(dir)) {
		int	len;

		if (strncmp(
			jjn.getClass().c_str(),
			de->d_name,
			jjn.getClass().size()))
		{
			continue;
		}

		len = strlen(de->d_name);
		if (len < 3)
			continue;

		if (strcmp(".bc", de->d_name + (len-3)))
			continue;

		bcpath = (path + jjn.getDir() + "/") + de->d_name;
		v.insert(bcpath);
	}

	closedir(dir);

	return v;
}


strset_t KModuleJ::findGlobalClassPaths(void)
{
	strset_t	st;

	/* load classes with global decls */
	foreach (it, module->global_begin(), module->global_end()) {
		std::string	s;
		JnJavaName	*jjn;

		if (it->isDeclaration() == false)
			continue;

//		if (seen_globals.count(it->getName().str()))
//			continue;

		jjn = JnJavaName::createGlobal(it->getName().str());
		if (jjn == NULL)
			continue;

		strset_t	t(findJJNClassPaths(*jjn));
		st.insert(t.begin(), t.end());
		delete jjn;
	}

	return st;
}


strset_t KModuleJ::findJJNClassPaths(const JnJavaName& jjn)
{
	strset_t	ss;

	if (!jjn.isAnonymous()) {
		ss.insert(classPath + ("/" + jjn.getBCPath()));
	} else {
		strset_t	t(findAnonClassPaths(jjn));
		ss.insert(t.begin(), t.end());
	}

	return ss;
}

void KModuleJ::dedupMod(Module *m) const
{
	/* ignore duplicates */
	foreach (it, m->begin(), m->end()) {
		Function	*f(&*it);
		std::string	name;

		if (f->isDeclaration())
			continue;

		name = f->getName().str();
		if (module->getFunction(name) == NULL)
			continue;

		/* oops, dup. */
		f->deleteBody();
		// if (isSafeDup(name))
		// 	continue;
		// std::cerr << "[KModule] DEDUPING-F: " << name << '\n';
	}

	std::vector<GlobalVariable*>	gvs;

	foreach (it, m->global_begin(), m->global_end())
		gvs.push_back(&*it);
	
	foreach (it, gvs.begin(), gvs.end()) {
		GlobalVariable	*gv, *m_gv(*it);

		gv = module->getGlobalVariable(m_gv->getName());
		if (	gv == NULL ||
			gv->isDeclaration() ||
			gv->isExternallyInitialized() ||
			m_gv->isExternallyInitialized())
			continue;

		std::vector<Use*>	u(m_gv->use_begin(), m_gv->use_end());
		foreach (it2, u.begin(), u.end()) {
			(*it2)->replaceUsesOfWith(m_gv, gv);
		}
		m_gv->setExternallyInitialized(true);
		m_gv->eraseFromParent();
	}
}

strset_t KModuleJ::findNeededFiles(void)
{
	strset_t	ret, s;

	s = findGlobalClassPaths();
	ret.insert(s.begin(), s.end());

	s = findFuncClassPaths();
	ret.insert(s.begin(), s.end());

	return ret;
}


void KModuleJ::loadClassByName(const JnJavaName& jjn)
{
#if 0
	std::string	path;

	/* load all anonymous files */
	if (jjn.isAnonymous() == true) {
//		loadAnonymousClassByName(jjn);
		assert (0 == 1);
		return;
	}

	path = (std::string(classPath) + "/") + jjn.getBCPath();
	loadClassByPath(path);
#endif
}

void KModuleJ::loadDeclsFromClassPath(void)
{
	strset_t	last_needed;
	strset_t	seen;

	while (1) {
		std::string	err;
		strset_t	needed;

		std::cerr << "DECL LOOPING\n";

		needed = findNeededFiles();

		if (needed.empty())
			break;

		if (needed == last_needed) {
			std::cerr << "All paths weren't resolved?\n";
			break;
		}

		foreach (it, needed.begin(), needed.end()) {
			Module		*m;
			bool		isBadLink;

			if (last_needed.count(*it)) {
				//std::cerr << "[KModule] Already have " << *it << '\n';
				continue;
			}

			m = getBitcodeModule(it->c_str());
			if (m == NULL) {
				std::cerr << "[KModuleJ] Couldn't load "
					<< *it << '\n';
			}
			dedupMod(m);

			std::cerr << "[KModule] LINKING IN " << (*it) << '\n';

			/* link in all undeclared modules */
			isBadLink = Linker::LinkModules(
				module,
				m,
				Linker::PreserveSource,
				&err);

			if (isBadLink)
				std::cerr << "[KModule] Error: " << err << '\n';
		}

		last_needed = needed;
		std::cerr << "===================\n";
	}
}

void KModuleJ::prepare(InterpreterHandler *ihandler)
{
	FunctionPassManager	_fpm(module);

	loadDeclsFromClassPath();

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
