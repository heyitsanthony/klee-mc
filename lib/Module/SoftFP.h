#ifndef SOFTFP_H
#define SOFTFP_H

#include "llvm/BasicBlock.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/CodeGen/IntrinsicLowering.h"

namespace llvm {
  class Function;
  class Instruction;
  class Module;
  class TargetData;
  class Type;
  class IntrinsicInst;
  class TargetLowering;
  class TargetMachine;
}

namespace klee {
class KModule;
class KFunction;

class SoftFPPass : public llvm::FunctionPass
{
public:
	SoftFPPass(KModule* _km);
	virtual ~SoftFPPass() {}

	virtual bool runOnFunction(llvm::Function &F);
private:
	bool replaceInst(llvm::Instruction* inst);
	bool replaceFCmp(llvm::Instruction* inst);
	llvm::Function* getCastThunk(
		llvm::Function* f,
		llvm::Type* retType,
		llvm::Value* arg0, llvm::Value* arg1 = NULL);

	llvm::Function* getUnorderedStub(
		llvm::Function* f,
		llvm::Value* arg0,
		llvm::Value* arg1);

	llvm::Function* getOrderedStub(
		llvm::Function* f,
		llvm::Value* arg0,
		llvm::Value* arg1);

	llvm::BasicBlock* setupFuncEntry(
		const std::string& newname,
		llvm::Type* retType,
		llvm::Value* arg0, llvm::Value* arg1,
		llvm::Value** v_arg0, llvm::Value** v_arg1);

	static char	ID;

	KModule		*km;
	KFunction	*f_fptrunc, *f_fpext,

			*f_fp32tosi32, *f_fp64tosi64,
			*f_fp64tosi32, *f_fp32tosi64,

			*f_si32tofp32, *f_si32tofp64,
			*f_si64tofp32, *f_si64tofp64,

			*f_fp32isnan, *f_fp64isnan,
			*f_fp32sqrt, *f_fp64sqrt,

			*f_fp32add, *f_fp32sub,
			*f_fp32mul, *f_fp32div, *f_fp32rem,
			*f_fp64add, *f_fp64sub,
			*f_fp64mul, *f_fp64div, *f_fp64rem,

			*f_fp32eq, *f_fp64eq,
			*f_fp32lt, *f_fp64lt,
			*f_fp32le, *f_fp64le;

};
}

#endif
