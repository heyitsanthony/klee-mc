#ifndef KLEE_HOOKPASS_H
#define KLEE_HOOKPASS_H

#include <map>
#include <llvm/Pass.h>

namespace llvm
{
	class Function;
}

namespace klee
{
class KModule;
class KFunction;
/// HookPass - This pass raises some common occurences of inline
/// asm which are used by glibc into normal LLVM IR.
class HookPass : public llvm::FunctionPass
{
private:
	static char ID;

	typedef std::map<std::string, KFunction*> fnmap_t;

	fnmap_t	f_pre;
	fnmap_t f_post;
	KModule	*kmod;

	bool hookPre(KFunction* kf, llvm::Function& f);
	bool hookPost(KFunction* kf, llvm::Function& f);

public:
	HookPass(KModule* _km);
	virtual ~HookPass();
	virtual bool runOnFunction(llvm::Function& f);
};
}
#endif