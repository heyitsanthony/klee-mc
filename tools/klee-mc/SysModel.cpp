#include <iostream>
#include <fstream>
#include <llvm/IR/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/LLVMContext.h>
#include "guestsnapshot.h"

#include "../../lib/Core/Globals.h"
#include "klee/Internal/Module/KModule.h"
#include "ExeStateVex.h"
#include "ExecutorVex.h"
#include "SyscallSFH.h"
#include "FdtSFH.h"
#include <sys/utsname.h>

#include "SysModel.h"

using namespace llvm;
using namespace klee;

extern bool UseConcreteVFS;
bool DenySysFiles;

namespace {
	cl::opt<bool,true> DenySysFilesProxy(
		"deny-sys-files",
		cl::desc("Fail attempts to open system files."),
		cl::location(DenySysFiles),
		cl::init(false));
}

static const char* guest2rtlib(const Guest* g)
{
	switch (g->getArch()) {
	case Arch::X86_64:
		return "libkleeRuntimeMC-amd64.bc";
	case Arch::ARM:
		return "libkleeRuntimeMC-arm.bc";
	case Arch::I386:
		return "libkleeRuntimeMC-x86.bc";
	default:
		assert (0 == 1 && "ULP");
		break;
	}
	return NULL;
}

LinuxModel::LinuxModel(Executor* e)
: SysModel(e, guest2rtlib(((ExecutorVex*)e)->getGuest()))
{}

W32Model::W32Model(Executor* e, const char* path)
: SysModel(e, path)
, gs(dynamic_cast<GuestSnapshot*>(((ExecutorVex*)e)->getGuest()))
{ assert (gs != NULL && "Expected snapshot for w32"); }

FDTModel::FDTModel(Executor* e) : SysModel(e, "libkleeRuntimeMC-fdt.bc") {}

//TODO: declare in kmodule h
Function *getStubFunctionForCtorList(
	Module *m,
	GlobalVariable *gv,
	std::string name);


static std::set<std::string> sys_missing;

void SysModel::setModelBool(Module* m, const char* gv_name, bool bv)
{
	GlobalVariable	*gv;
	Constant	*initer;
	Type		*t;

	gv = static_cast<GlobalVariable*>(m->getGlobalVariable(gv_name));
	if (gv == NULL) {
		if (sys_missing.count(gv_name)) return;
		sys_missing.insert(gv_name);
		std::cerr << "[SysModel] Could not find " << gv_name << '\n';
		return;
	}

	assert (gv != NULL);

	initer = gv->getInitializer();
	t = IntegerType::get(
		getGlobalContext(),
		initer->getType()->getPrimitiveSizeInBits());
	gv->setInitializer(bv
		? ConstantInt::get(t, 1)
		: ConstantInt::get(t, 0));

	gv->setConstant(true);
}

void SysModel::setModelU64(Module* m, const char* gv_name, uint64_t bv)
{
	GlobalVariable	*gv;

	gv = static_cast<GlobalVariable*>(m->getGlobalVariable(gv_name));
	if (gv == NULL) {
		if (sys_missing.count(gv_name)) return;
		sys_missing.insert(gv_name);
		std::cerr << "[SysModel] Could not find " << gv_name << '\n';
		return;
	}

	assert (gv != NULL);
	gv->setInitializer(
		ConstantInt::get(getGlobalContext(), APInt(64, bv, false)));
	gv->setConstant(true);
}


void FDTModel::installInitializers(Function *init_func)
{
	GlobalVariable	*ctors;
	KModule		*kmodule;

	kmodule = exe->getKModule();
	ctors = kmodule->module->getNamedGlobal("llvm.global_ctors");
	std::cerr << "checking for global ctors and dtors" << std::endl;
	if (ctors) {
		Function* ctorStub;

		std::cerr << "[fdt] installing ctors" << std::endl;
		ctorStub = getStubFunctionForCtorList(
			kmodule->module, ctors, "klee.ctor_stub");
		kmodule->addFunction(ctorStub);
		exe->addInitFunction(ctorStub);
	}

	// TODO
	// can't install detours because this function returns almost immediately
	// GlobalVariable *dtors =
	// 	kmodule->module->getNamedGlobal("llvm.global_dtors");
	// do them later
	// if (dtors) {
	// 	std::cerr << "installing dtors" << std::endl;
	// 	Function *dtorStub;
	// 	dtorStub = getStubFunctionForCtorList(
	// 		kmodule->module, dtors, "klee.dtor_stub");
	// 	kmodule->addFunction(dtorStub);
	// 	foreach (it, init_func->begin(), init_func->end()) {
	// 		if (!isa<ReturnInst>(it->getTerminator())) continue;
	// 		CallInst::Create(dtorStub, "", it->getTerminator());
	// 	}
	// }
	setModelBool(kmodule->module, "concrete_vfs", UseConcreteVFS);
}

void FDTModel::installConfig(ExecutionState& state)
{
	struct utsname	buf;
	uname(&buf);
	installData(state, "g_utsname", &buf, sizeof(buf));
}

void SysModel::installData(
	ExecutionState& state,
	const char* name,
	const void* data,
	unsigned int len)
{
	GlobalVariable	*g;
	MemoryObject	*mo;
	ObjectState	*os;

	g = static_cast<GlobalVariable*>(
		exe->getKModule()->module->getGlobalVariable(name));
	assert (g != NULL && "Could not find install variable");

	mo = exe->getGlobals()->findObject(g);
	os = state.addressSpace.findWriteableObject(mo);

	assert(os != NULL);
	assert (mo->size >= len);

	for (unsigned i = 0; i < len; i++)
		state.write8(os, i, ((unsigned char*)&data)[i]);

}

SyscallSFH* FDTModel::allocSpecialFuncHandler(Executor* e) const
{ return new FdtSFH(e); }

SyscallSFH* LinuxModel::allocSpecialFuncHandler(Executor* e) const
{ return new SyscallSFH(e); }


SyscallSFH* W32Model::allocSpecialFuncHandler(Executor* e) const
{ return new SyscallSFH(e); }

void LinuxModel::installConfig(ExecutionState& es)
{
	setModelBool(exe->getKModule()->module, "concrete_vfs", UseConcreteVFS);
	setModelBool(exe->getKModule()->module, "deny_sys_files", DenySysFiles);
}

void W32Model::installConfig(ExecutionState& state)
{
	char		pbi[24];
	uint32_t	version = 0x0105; /* default = winxp */
	bool		ok;
	uint32_t	cookie;

	ok = gs->getPlatform("pbi", &pbi, 24);
	assert (ok == true);
	installData(state, "plat_pbi", pbi, sizeof(pbi));

	ok = gs->getPlatform("process_cookie", &pbi, 4);
	assert (ok == true);
	installData(state, "plat_cookie", &cookie, sizeof(cookie));

	gs->getPlatform("version", &version, sizeof(version));
	installData(state, "plat_version", &version, sizeof(version));
}

class NoneSFH : public SyscallSFH
{
public:
	NoneSFH(Executor* es) : SyscallSFH(es) {}
	virtual ~NoneSFH() {}
};

SyscallSFH* NoneModel::allocSpecialFuncHandler(Executor* e) const
{ return new NoneSFH(e); }
