#ifndef KMODULEJ_H
#define KMODULEJ_H

#include "klee/Internal/Module/KModule.h"

class JnJavaName;

namespace klee
{
typedef std::set<std::string>	strset_t;
class KModuleJ : public KModule
{
public:
	KModuleJ(llvm::Module *_m, const ModuleOptions &_o)
	: KModule(_m, _o) {}
	virtual ~KModuleJ(void);
	virtual KFunction* addFunction(llvm::Function* f);
	virtual void prepare(InterpreterHandler *ihandler);
	KFunction* getEntryFunction(void) const;
private:
	void loadClassByName(const JnJavaName& jjn);
	
	strset_t findAnonClassPaths(const JnJavaName& jjn);
	strset_t findGlobalClassPaths(void);
	strset_t findFuncClassPaths(void);
	strset_t findJJNClassPaths(const JnJavaName& jjn);
	strset_t findNeededFiles(void);

	void dedupMod(llvm::Module *m) const;

	void loadDeclsFromClassPath(void);


	void loadClassByPath(const std::string& s);


	typedef	std::map<llvm::Function*, JnJavaName*> f2jjn_t;
	f2jjn_t	f2jjn;
};

}
#endif
