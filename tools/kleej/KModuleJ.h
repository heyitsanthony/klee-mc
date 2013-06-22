#ifndef KMODULEJ_H
#define KMODULEJ_H

#include "klee/Internal/Module/KModule.h"

class JnJavaName;

namespace klee
{
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
	typedef	std::map<llvm::Function*, JnJavaName*> f2jjn_t;
	f2jjn_t	f2jjn;
};

}
#endif
