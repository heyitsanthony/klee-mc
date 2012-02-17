#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/IRBuilder.h>
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Module/KFunction.h"
#include "Passes.h"
#include "static/Sugar.h"
#include <iostream>

using namespace llvm;
using namespace klee;

struct func_names
{
	const char*	name;
	KFunction**	kf_loc;
};

namespace klee { extern Module* getBitcodeModule(const char* path); }

char SoftFPPass::ID;

SoftFPPass::SoftFPPass(KModule* _km, const char* _dir)
: llvm::FunctionPass(ID)
, km(_km)
{
	llvm::Module	*mod;
	llvm::sys::Path path(_dir);
	struct func_names	fns[] = {
		{"float64_to_float32", &f_fptrunc},
		{"float32_to_float64", &f_fpext},

		{"float32_to_int32", &f_fp32tosi32},
		{"float32_to_int64", &f_fp32tosi64},
		{"float64_to_int32", &f_fp64tosi32},
		{"float64_to_int64", &f_fp64tosi64},

		{"int32_to_float32", &f_si32tofp32},
		{"int32_to_float64", &f_si32tofp64},

		{"int64_to_float32", &f_si64tofp32},
		{"int64_to_float64", &f_si64tofp64},



		{"float32_add", &f_fp32add},
		{"float64_add", &f_fp64add},
		{"float32_sub", &f_fp32sub},
		{"float64_sub", &f_fp64sub},
		{"float32_mul", &f_fp32mul},
		{"float64_mul", &f_fp64mul},
		{"float32_div", &f_fp32div},
		{"float64_div", &f_fp64div},
		{"float32_rem", &f_fp32rem},
		{"float64_rem", &f_fp64rem},

		{"float32_eq", &f_fp32eq},
		{"float64_eq", &f_fp64eq},

		{"float32_lt", &f_fp32lt},
		{"float64_lt", &f_fp64lt},

		{"float32_le", &f_fp32le},
		{"float64_le", &f_fp64le},

		{NULL, NULL}
	};

	path.appendComponent("softfloat.bc");
	mod = getBitcodeModule(path.c_str());
	assert (mod != NULL);

	km->addModule(mod);
	for (unsigned i = 0; fns[i].name != NULL; i++) {
		KFunction	*kf;
		kf = km->getKFunction(fns[i].name);
		assert (kf != NULL);
		*(fns[i].kf_loc) = kf;
	}

	delete mod;
}

bool SoftFPPass::runOnFunction(llvm::Function &F)
{
	bool	changed = false;

	foreach (bit, F.begin(), F.end()) {
		for (	BasicBlock::iterator i = bit->begin(), ie = bit->end();
			i != ie;)
		{
			Instruction	*inst = &*i;

			i++;
			changed |= replaceInst(inst);
		}
	}

	return changed;
}

bool SoftFPPass::replaceInst(Instruction* inst)
{
	BasicBlock::iterator	ii(inst);
	Type			*ty;
	Value			*v0;

	v0 = (inst->getNumOperands() > 0) ? inst->getOperand(0) : NULL;
	ty = inst->getType();
	switch (inst->getOpcode()) {
	case Instruction::FPExt: {
		CallInst		*fpext_call;

		assert (ty->getPrimitiveSizeInBits() == 64 &&
			v0->getType()->getPrimitiveSizeInBits() == 32);

		fpext_call = CallInst::Create(f_fpext->function, v0);
		ReplaceInstWithInst(
			inst->getParent()->getInstList(),
			ii,
			fpext_call);
		return true;
	}
	break;

#define OP_REPL(x,y)	\
	case Instruction::F##x: {	\
		CallInst		*fp_call;	\
		Function		*f;	\
		Value			*args[] = {v0, inst->getOperand(1)};	\
		unsigned		ty_w;	\
\
		ty_w  = ty->getPrimitiveSizeInBits();	\
		assert(ty_w == 32 || ty_w == 64);	\
		f = (ty_w == 32) ? f_fp32##y->function : f_fp64##y->function;	\
\
		fp_call = CallInst::Create(f, ArrayRef<Value*>(args, 2));	\
		ReplaceInstWithInst(inst, fp_call);	\
		return true; }

	OP_REPL(Add, add)
	OP_REPL(Sub, sub)
	OP_REPL(Mul, mul)
	OP_REPL(Div, div)

	case Instruction::FCmp: {
		FCmpInst 	*fi;
		CmpInst::Predicate pred;
		unsigned	ty_w = v0->getType()->getPrimitiveSizeInBits();
		IRBuilder<>	irb(inst);
		Value		*v;
		Function	*f;
		bool		f_flip;

		fi = cast<FCmpInst>(inst);
		assert (ty_w == 32 || ty_w == 64);

		std::cerr << "[FP] GET NANs working right: ";

		// Ordered comps return false if either operand is NaN.
		// Unordered comps return true if either operand is NaN.
		pred = fi->getPredicate();
		f_flip = false;
		if (pred == FCmpInst::FCMP_OEQ)
			f = (ty_w == 32)
				? f_fp32eq->function
				: f_fp64eq->function;
		else if (pred == FCmpInst::FCMP_OLT)
			f = (ty_w == 32)
				? f_fp32lt->function
				: f_fp64lt->function;
		else if (pred == FCmpInst::FCMP_OGT) {
			f = (ty_w == 32)
				? f_fp32lt->function
				: f_fp64lt->function;
			f_flip = true;
		} else
			assert (0 == 1 && "???");

		if (f_flip == false) {
			v = irb.CreateCall2(f, v0, inst->getOperand(1));
		} else {
			v = irb.CreateCall2(f, inst->getOperand(1), v0);
		}

		v = irb.CreateTrunc(
			v,
			IntegerType::get(ty->getContext(), 1));

		ReplaceInstWithInst(
			inst,
			static_cast<Instruction*>(v));
		return true;
	}

	case Instruction::FPToSI: {
		CallInst		*fp_call;
		Function		*f;
		unsigned		ty_w, v_w;

		ty_w  = ty->getPrimitiveSizeInBits();
		v_w = v0->getType()->getPrimitiveSizeInBits();
		assert ((ty_w == 32 || ty_w == 64) &&
			(v_w == 32 || v_w == 64));

		if (v_w == 32) {
			f = (ty_w == 32)
				? f_fp32tosi32->function
				: f_fp32tosi64->function;
		} else {
			f = (ty_w == 32)
				? f_fp64tosi32->function
				: f_fp64tosi64->function;
		}

		fp_call = CallInst::Create(f, v0);
		ReplaceInstWithInst(inst, fp_call);
		return true;
	}

	case Instruction::SIToFP: {
		CallInst		*fp_call;
		Function		*f;
		unsigned		ty_w, v_w;

		ty_w  = ty->getPrimitiveSizeInBits();
		v_w = v0->getType()->getPrimitiveSizeInBits();
		assert ((ty_w == 32 || ty_w == 64) &&
			(v_w == 32 || v_w == 64));

		if (v_w == 32) {
			f = (ty_w == 32)
				? f_si32tofp32->function
				: f_si32tofp64->function;
		} else {
			f = (ty_w == 32)
				? f_si64tofp32->function
				: f_si64tofp64->function;
		}

		fp_call = CallInst::Create(f, v0);
		ReplaceInstWithInst(inst, fp_call);
		return true;
	}

	case Instruction::FPTrunc: {
		CallInst		*fp_call;
		Function		*f;
		unsigned		ty_w, v_w;

		std::cerr << "[FP] worry about rounding modes more\n";
		ty_w  = ty->getPrimitiveSizeInBits();
		v_w = v0->getType()->getPrimitiveSizeInBits();
		assert (ty_w == 32 &&  v_w == 64);

		f = f_fptrunc->function;
		fp_call = CallInst::Create(f, v0);
		ReplaceInstWithInst(inst, fp_call);
		return true;
	}

	case Instruction::FRem:
	case Instruction::FPToUI:
	case Instruction::UIToFP:
		inst->dump();
		std::cerr << "\n=======FUNC======\n";
		inst->getParent()->getParent()->dump();
		assert (0 == 1 && "IMPLEMENT ME!!!!!!");
	break;

	}

	return false;
}