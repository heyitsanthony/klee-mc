//===-- ExternalDispatcher.cpp --------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ExternalDispatcher.h"
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/IR/CallSite.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <set>
#include <setjmp.h>
#include <signal.h>
#include <iostream>

using namespace llvm;
using namespace klee;

/***/

static jmp_buf escapeCallJmpBuf;

extern "C" {
static void sigsegv_handler(int signal, siginfo_t *info, void *context)
{ longjmp(escapeCallJmpBuf, 1); }
}

namespace klee
{
class DispatcherMem : public llvm::SectionMemoryManager
{
public:
	DispatcherMem(void) {}
	virtual ~DispatcherMem() {}

	DispatcherMem(const DispatcherMem&) = delete;
	void operator=(const DispatcherMem&) = delete;

	std::unique_ptr<SectionMemoryManager> createProxy(void);

	void* getPointerToNamedFunction(const std::string &name, bool abort_on_fail)
		override;

	uint64_t getSymbolAddress(const std::string& n) override {
		uint64_t addr;
		addr = SectionMemoryManager::getSymbolAddress(n);
		if (addr) return addr;
		return 0; //(uint64_t)jit_engine.getPointerToNamedFunction(n);
	}

private:
};

void* DispatcherMem::getPointerToNamedFunction(
	const std::string &name,
	bool abort_on_fail)
{
	const char *str = name.c_str();

	// We use this to validate that function names can be resolved so we
	// need to match how the JIT does it. Unfortunately we can't
	// directly access the JIT resolution function
	// JIT::getPointerToNamedFunction so we emulate the important points.

	if (str[0] == 1) // asm specifier, skipped
		++str;

	void *addr = sys::DynamicLibrary::SearchForAddressOfSymbol(str);
	if (addr) return addr;

	// If it has an asm specifier and starts with an underscore we retry
	// without the underscore. I (DWD) don't know why.
	if (name[0] == 1 && str[0]=='_') {
		++str;
		addr = sys::DynamicLibrary::SearchForAddressOfSymbol(str);
	}

	if (!addr && abort_on_fail) abort();

	return addr;
}
}

// since SMM is destroyed with each exe engine, have a sacrificial class
// that only passes method calls to the shared DispatcherMem object
class DispatcherMemProxy : public SectionMemoryManager
{
public:
	DispatcherMemProxy(SectionMemoryManager& in_smm) : smm(in_smm) {}

	DispatcherMemProxy(const DispatcherMemProxy&) = delete;
	void operator=(const DispatcherMemProxy&) = delete;

	void* getPointerToNamedFunction(const std::string &name, bool abort_on_fail)
		override
	{ return smm.getPointerToNamedFunction(name, abort_on_fail); }

	uint64_t getSymbolAddress(const std::string& n) override
	{ return smm.getSymbolAddress(n); }

	uint8_t *allocateCodeSection(
		uintptr_t Size, unsigned Alignment, unsigned SectionID,
		StringRef SectionName) override

	{
		return smm.allocateCodeSection(
			Size, Alignment, SectionID, SectionName);
	}

	uint8_t *allocateDataSection(
		uintptr_t Size, unsigned Alignment,
		unsigned SectionID, StringRef SectionName,
		bool isReadOnly) override
	{
		return smm.allocateDataSection(
			Size, Alignment, SectionID, SectionName, isReadOnly);
	}

	void reserveAllocationSpace(
		uintptr_t CodeSize, uintptr_t DataSizeRO,
		uintptr_t DataSizeRW) override
	{
		smm.reserveAllocationSpace(CodeSize, DataSizeRO, DataSizeRW);
	}

	bool finalizeMemory (std::string *ErrMsg=nullptr) override {
		return smm.finalizeMemory(ErrMsg);
	}

	// nooooo
	void deregisterEHFrames(
		uint8_t *addr, uint64_t loadaddr, size_t size) override
	{
		return;
	}

private:
	SectionMemoryManager	&smm;
};

// compiler can't figure out DispatcherMemProxy <: SectionMemoryManager
std::unique_ptr<SectionMemoryManager> DispatcherMem::createProxy(void)
{
	return std::make_unique<DispatcherMemProxy>(*this);
}


void* ExternalDispatcher::resolveSymbol(const std::string& name) const
{
	return (void*)(smm->getSymbolAddress(name));
}


void ExternalDispatcher::buildExeEngine(void)
{
	std::string		error;

	exeEngine = std::unique_ptr<ExecutionEngine>(
		EngineBuilder(std::move(dispatchModule))
			.setErrorStr(&error)
			.setEngineKind(EngineKind::JIT)
			.setMCJITMemoryManager(smm->createProxy())
			.create());
	if (!exeEngine) {
		std::cerr << "unable to make jit: " << error << "\n";
		abort();
	}

	exeEngine->finalizeObject();
}

ExternalDispatcher::ExternalDispatcher()
	: smm(std::make_unique<DispatcherMem>())
{
	// If we have a native target, initialize it to ensure it is linked in and
	// usable by the JIT.
	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmParser();
	llvm::InitializeNativeTargetAsmPrinter();

	// Make sure we can resolve symbols in the program as well.
	// The zero arg to the function tells DynamicLibrary to load
	// the program, not a library.
	sys::DynamicLibrary::LoadLibraryPermanently(0);

#ifdef WINDOWS
	preboundFunctions["getpid"] = (void*) (long) getpid;
	preboundFunctions["putchar"] = (void*) (long) putchar;
	preboundFunctions["printf"] = (void*) (long) printf;
	preboundFunctions["fprintf"] = (void*) (long) fprintf;
	preboundFunctions["sprintf"] = (void*) (long) sprintf;
#endif
}

ExternalDispatcher::~ExternalDispatcher() {}

llvm::Function* ExternalDispatcher::findDispatchThunk(
	llvm::Function* f,
	llvm::Instruction *i)
{
	llvm::Function	*dispatch_thunk;

	auto it = dispatchers.find(i);
	if (it != dispatchers.end())
		return it->second;

#ifdef WINDOWS
	auto it2 = preboundFunctions.find(f->getName());
	if (it2 != preboundFunctions.end()) {
		// only bind once
		if (it2->second) {
			exeEngine->addGlobalMapping(f, it2->second);
			it2->second = 0;
		}
	}
#endif

	dispatch_thunk = createDispatcherThunk(f, i);
	dispatchers.insert(std::make_pair(i, dispatch_thunk));

	return dispatch_thunk;
}

bool ExternalDispatcher::executeCall(Function *f, Instruction *i, uint64_t *args)
{
	llvm::Function		*thunk;
	dispatch_jit_fptr_t	fptr;
	
	thunk = findDispatchThunk(f, i);

	// already jitted?
	auto it = dispatches_jitted.find(thunk);
	if (it != dispatches_jitted.end()) {
		return runProtectedCall(it->second, args);
	}


	if (dispatchModule) buildExeEngine();

	fptr = (dispatch_jit_fptr_t)exeEngine->getPointerToFunction(thunk);
	if (!fptr) {
		std::cerr << "Couldn't JIT thunk\n";
		return false;
	}
	dispatches_jitted[thunk] = fptr;

	return runProtectedCall(fptr, args);
}

// FIXME: This is not reentrant.
static uint64_t *gTheArgsP;

bool ExternalDispatcher::runProtectedCall(dispatch_jit_fptr_t f, uint64_t *args)
{
	struct sigaction segvAction, segvActionOld;
	bool res;

	if (!f) return false;

	gTheArgsP = args;

	segvAction.sa_handler = 0;
	memset(&segvAction.sa_mask, 0, sizeof(segvAction.sa_mask));
	segvAction.sa_flags = SA_SIGINFO;
	segvAction.sa_sigaction = ::sigsegv_handler;
	sigaction(SIGSEGV, &segvAction, &segvActionOld);

	if (setjmp(escapeCallJmpBuf)) {
		res = false;
	} else {
		f();
		res = true;
	}

	sigaction(SIGSEGV, &segvActionOld, 0);
	return res;
}

// For performance purposes we construct the stub in such a way that the
// arguments pointer is passed through the static global variable gTheArgsP in
// this file. This is done so that the stub function prototype trivially matches
// the special cases that the JIT knows how to directly call. If this is not
// done, then the jit will end up generating a nullary stub just to call our
// stub, for every single function call.
Function *ExternalDispatcher::createDispatcherThunk(
	Function *target, Instruction *inst)
{
	CallSite cs;

	if (!resolveSymbol(target->getName()))
		return 0;

	if (!dispatchModule) {
		dispatchModule = std::make_unique<Module>(
				"ExternalDispatcher",
				getGlobalContext());
	}

	if (inst->getOpcode()==Instruction::Call) {
		cs = CallSite(cast<CallInst>(inst));
	} else {
		cs = CallSite(cast<InvokeInst>(inst));
	}

	std::vector<Value*> args(cs.arg_size());
	std::vector<Type*> nullary;

	Function *dispatcher = Function::Create(
		FunctionType::get(
			Type::getVoidTy(getGlobalContext()), nullary, false),
			GlobalVariable::ExternalLinkage,
			"",
			dispatchModule.get());


	BasicBlock *dBB = BasicBlock::Create(getGlobalContext(), "entry", dispatcher);

	// Get a Value* for &gTheArgsP, as an i64**.
	Instruction *argI64sp =  new IntToPtrInst(
	ConstantInt::get(
		Type::getInt64Ty(getGlobalContext()),
		(uintptr_t) (void*) &gTheArgsP),
		PointerType::getUnqual(
			PointerType::getUnqual(
				Type::getInt64Ty(getGlobalContext()))),
		"argsp",
		dBB);

	Instruction *argI64s = new LoadInst(argI64sp, "args", dBB);

	// Get the target function type.
	FunctionType *FTy = cast<FunctionType>(
		cast<PointerType>(target->getType())->getElementType());

	// Each argument will be passed by writing it into gTheArgsP[i].
	unsigned i = 0, idx = 2;
	for (	CallSite::arg_iterator ai = cs.arg_begin(), ae = cs.arg_end();
		ai!=ae; ++ai, ++i) {
	// Determine the type the argument will be passed as.
	// This accomodates for the code in Executor.cpp for handling calls
	// to bitcast functions.
		Type *argTy = (i < FTy->getNumParams())
			? FTy->getParamType(i)
			: (*ai)->getType();
		Instruction *argI64p;
		
		argI64p = GetElementPtrInst::Create(
			argI64s,
			ConstantInt::get(
				Type::getInt32Ty(getGlobalContext()),
				idx),
			"",
			dBB);

		Instruction *argp;
		
		argp = new BitCastInst(
			argI64p, PointerType::getUnqual(argTy), "", dBB);
		args[i] = new LoadInst(argp, "", dBB);

		unsigned argSize = argTy->getPrimitiveSizeInBits();
		idx += ((!!argSize ? argSize : 64) + 63)/64;
	}

	args.resize(i);
	Instruction *result = CallInst::Create(target, args, "", dBB);
	if (!result->getType()->isVoidTy()) {
		Instruction *resp;
		resp = new BitCastInst(
			argI64s,
			PointerType::getUnqual(result->getType()),
			"",
			dBB);
		new StoreInst(result, resp, dBB);
	}

	ReturnInst::Create(getGlobalContext(), dBB);

	return dispatcher;
}
