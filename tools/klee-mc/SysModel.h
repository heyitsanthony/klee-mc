#ifndef SYSMODEL_H

#include <stdint.h>

class GuestSnapshot;

namespace llvm
{
class Function;
class Module;
}

namespace klee
{
class ExecutionState;
class Executor;
class SyscallSFH;

class SysModel
{
public:
	virtual ~SysModel() {}
	const char* getModelFileName(void) const { return fname; }
	virtual void installInitializers(llvm::Function* f) = 0;
	virtual void installConfig(ExecutionState& state) = 0;
	virtual SyscallSFH* allocSpecialFuncHandler(Executor*) const = 0;

	void setModelBool(llvm::Module* m, const char* gv_name, bool bv);
	void setModelU64(llvm::Module* m, const char* gv_name, uint64_t bv);

	void installData(
		ExecutionState& state,
		const char* name,
		const void* data, 
		unsigned int len);
protected:
	SysModel(Executor* e, const char* in_fname)
	: exe(e), fname(in_fname) {}

	Executor*	exe;
private:
	const char	*fname;
};

class LinuxModel : public SysModel
{
public:
	LinuxModel(Executor* e);
	virtual ~LinuxModel(void) {}
	SyscallSFH* allocSpecialFuncHandler(Executor*) const;
	void installInitializers(llvm::Function* f) {}
	void installConfig(ExecutionState& state);
};

class W32Model : public SysModel
{
public:
	W32Model(Executor* e);
	virtual ~W32Model(void) {}
	SyscallSFH* allocSpecialFuncHandler(Executor*) const;
	void installInitializers(llvm::Function* f) {}
	void installConfig(ExecutionState& state);
private:
	GuestSnapshot	*gs;
};

class FDTModel : public SysModel
{
public:
	FDTModel(Executor* e);
	virtual ~FDTModel(void) {}
	SyscallSFH* allocSpecialFuncHandler(Executor*) const;
	void installInitializers(llvm::Function* f);
	void installConfig(ExecutionState& state);
};

class NoneModel : public SysModel 
{
public:
	NoneModel(Executor *e) : SysModel(e, "libkleeRuntimeMC-sysnone.bc") {}
	virtual ~NoneModel() {}
	virtual void installInitializers(llvm::Function* f) {}
	virtual void installConfig(ExecutionState& state) {}
	SyscallSFH* allocSpecialFuncHandler(Executor*) const;
};
}

#endif
