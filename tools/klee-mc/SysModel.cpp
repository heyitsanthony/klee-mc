#include <iostream>
#include <llvm/Module.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/IntrinsicInst.h>
#include <llvm/LLVMContext.h>

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
: SysModel(
	e,
	guest2rtlib(((ExecutorVex*)e)->getGuest()))
{}


FDTModel::FDTModel(Executor* e) : SysModel(e, "libkleeRuntimeMC-fdt.bc") {}

//TODO: declare in kmodule h
Function *getStubFunctionForCtorList(
	Module *m,
	GlobalVariable *gv,
	std::string name);


void SysModel::setModelBool(Module* m, const char* gv_name, bool bv)
{
	GlobalVariable	*gv;
	Constant	*initer;
	Type		*t;

	gv = static_cast<GlobalVariable*>(m->getGlobalVariable(gv_name));
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
		std::cerr << "installing ctors" << std::endl;
		Function* ctorStub;

		ctorStub = getStubFunctionForCtorList(
			kmodule->module, ctors, "klee.ctor_stub");
		kmodule->addFunction(ctorStub);
		CallInst::Create(
			ctorStub,
			"",
			init_func->begin()->begin());
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
	GlobalVariable	*g_utsname;
	MemoryObject	*mo;
	ObjectState	*os;
	struct utsname	buf;

	g_utsname = static_cast<GlobalVariable*>(
		exe->getKModule()->module->getGlobalVariable("g_utsname"));

	mo = exe->getGlobals()->findObject(g_utsname);
	os = state.addressSpace.findWriteableObject(mo);

	assert(os != NULL);

	uname(&buf);
	for (unsigned offset=0; offset < mo->size; offset++) {
		state.write8(os, offset, ((unsigned char*)&buf)[offset]);
	}
}

SyscallSFH* FDTModel::allocSpecialFuncHandler(Executor* e) const
{
	return new FdtSFH(e);
}

SyscallSFH* LinuxModel::allocSpecialFuncHandler(Executor* e) const
{
	return new SyscallSFH(e);
}

void LinuxModel::installInitializers(llvm::Function* f)
{
	setModelBool(exe->getKModule()->module, "concrete_vfs", UseConcreteVFS);
	setModelBool(exe->getKModule()->module, "deny_sys_files", DenySysFiles);
}
