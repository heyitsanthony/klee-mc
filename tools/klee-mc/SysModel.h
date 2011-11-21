#ifndef SYSMODEL_H

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
	LinuxModel(Executor* e) : SysModel(e, "libkleeRuntimeMC.bc") {}
	virtual ~LinuxModel(void) {}
	SyscallSFH* allocSpecialFuncHandler(Executor*) const;
	void installInitializers(llvm::Function* f);
	void installConfig(ExecutionState& state) {}
private:
};

class FDTModel : public SysModel
{
public:
	FDTModel(Executor* e) : SysModel(e, "libkleeRuntimeMC-fdt.bc") {}
	virtual ~FDTModel(void) {}
	SyscallSFH* allocSpecialFuncHandler(Executor*) const;
	void installInitializers(llvm::Function* f);
	void installConfig(ExecutionState& state);
};
}

#endif
