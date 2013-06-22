#include <llvm/Support/Path.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/IR/Module.h>
#include <llvm/Pass.h>
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Module/KFunction.h"
#include "static/Sugar.h"

#include "HookPass.h"

#include <iostream>
#include <sstream>

using namespace llvm;
using namespace klee;

namespace klee { extern Module* getBitcodeModule(const char* path); }

namespace
{
	llvm::cl::opt<std::string>
	HookPassLib(
		"hookpass-lib",
		llvm::cl::desc("Hook pass library."),
		llvm::cl::init("hookpass.bc"));
}

char HookPass::ID;

HookPass::HookPass(KModule* module)
: llvm::FunctionPass(ID)
, kmod(module)
{
	llvm::Module	*mod;
	llvm::sys::Path	path;

	if (HookPassLib[0] == '/' || HookPassLib[0] == '.') {
		path = HookPassLib;
	} else {
		path = kmod->getLibraryDir();
		path.appendComponent(HookPassLib.c_str());
	}
	std::cerr << "[HookPass] Using library '" << path.c_str() << "'\n";

	mod = getBitcodeModule(path.c_str());
	assert (mod != NULL);

	kmod->addModule(mod);

	foreach (it, mod->begin(), mod->end()) {
		const Function	*f(it);
		std::string	fn_s(f->getName().str());
		const char	*hooked_fn, *fn_c;

		fn_c = fn_s.c_str();
		if (!strncmp(fn_c, "__hookpre_", sizeof("__hookpre"))) {
			hooked_fn = fn_c + sizeof("__hookpre");
			f_pre[hooked_fn] = kmod->getKFunction(fn_c);
		} else if (!strncmp(fn_c, "__hookpost_", sizeof("__hookpost"))) {
			hooked_fn = fn_c + sizeof("__hookpost");
			f_post[hooked_fn] = kmod->getKFunction(fn_c);
		} else
			continue;

		std::cerr	<< "[HookPass] ADDING " << fn_s
				<< " (" << hooked_fn << ")\n";
	}

	delete mod;
}

HookPass::~HookPass() {}

bool HookPass::hookPre(KFunction* kf, llvm::Function& f)
{
	std::vector<Value*>	args;

	/* function arguments must match */
	assert (f.getArgumentList().size() ==
		kf->function->getArgumentList().size());

	/* this is disgusting, but necessary to pass assertion builds */
	Instruction	*ii = &f.getEntryBlock().front();
	unsigned	i = 0;
	foreach (it, f.arg_begin(), f.arg_end()) {
		Type	*f_t(f.getFunctionType()->getParamType(i)),
			*kf_t(kf->function->getFunctionType()->getParamType(i));

		if (f_t != kf_t) {
			args.push_back(
				CastInst::CreateZExtOrBitCast(
					&*it,
					kf_t,
					"",
					ii));
		} else
			args.push_back(&*it);

		i++;
	}

	/* insert function into very beginning of function */
	CallInst::Create(kf->function, args, "", ii);
	return true;
}

bool HookPass::hookPost(KFunction* kf, llvm::Function& f)
{
	bool changed = false;

	assert (kf->function->arg_size() == 1);

	assert (f.getReturnType() == (kf->function->arg_begin())->getType()
		&& "Post return type does not match function");

	foreach (it, f.begin(), f.end()) {
		BasicBlock		*bb(it);
		ReturnInst		*ri;
		std::vector<Value*>	args;

		ri = dyn_cast<ReturnInst>(bb->getTerminator());
		if (ri == NULL)
			continue;

		args.push_back(ri->getReturnValue());

		/* insert function immediately before return */
		CallInst::Create(kf->function, args, "posthook", ri);
		changed |= true;
	}

	return changed;
}

bool HookPass::runOnFunction(llvm::Function& f)
{
	std::string	s(f.getName().str());
	size_t		n;

	if (processFunc(f, s) == true)
		return true;

	s = kmod->getPrettyName(&f);
	n = s.find_last_of('+');
	if (n == std::string::npos)
		return false;

	if (s.substr(n+1) != "0x0")
		return false;

	/* maybe there's an offset suffix */
	if (processFunc(f, s.substr(0, n)))
		return true;

	return false;
}

bool HookPass::processFunc(llvm::Function& f, const std::string& fn_s)
{
	fnmap_t::const_iterator	it;
	bool			changed = false;

	if ((it = f_pre.find(fn_s)) != f_pre.end())
		changed |= hookPre(it->second, f);

	if ((it = f_post.find(fn_s)) != f_post.end())
		changed |= hookPost(it->second, f);

	if (changed) {
		std::cerr << "[HookPass] Hooked " << fn_s << '\n';
	}

	return changed;
}
