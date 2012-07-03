#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/IRBuilder.h>
#include "klee/Internal/Module/KModule.h"
#include "klee/Internal/Module/KFunction.h"
#include "Passes.h"
#include "static/Sugar.h"

#include <iostream>
#include <sstream>

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

		{"int32_to_float32", &f_si32tofp32},
		{"int32_to_float64", &f_si32tofp64},
		{"int64_to_float32", &f_si64tofp32},
		{"int64_to_float64", &f_si64tofp64},

#define DECL_FOP(x,y)	\
		{"float32_" #x, &f_fp32 ##y},\
		{"float64_" #x, &f_fp64 ##y},
		DECL_FOP(to_int32, tosi32)
		DECL_FOP(to_int64, tosi64)
		DECL_FOP(is_nan, isnan)
		DECL_FOP(sqrt, sqrt)
		DECL_FOP(add, add)
		DECL_FOP(sub, sub)
		DECL_FOP(mul, mul)
		DECL_FOP(div, div)
		DECL_FOP(rem, rem)
		DECL_FOP(eq, eq)
		DECL_FOP(lt, lt)
		DECL_FOP(le, le)
#undef DECL_FOP
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

/* bitcast FP arguments into Integer, bitcast Integer result into FP */
llvm::Function* SoftFPPass::getCastThunk(
	llvm::Function* f,
	llvm::Type	*retType,
	llvm::Value	*arg0,
	llvm::Value	*arg1)
{
	Module			*m;
	Function		*ret_f;
	BasicBlock		*bb;
	FunctionType		*ft;
	std::stringstream	namestream;
	std::string		newname;
	Argument		*arg_a, *arg_b;
	Type			*cast_type;
	Value			*v_ret, *v_arg0, *v_arg1;
	Function::ArgumentListType::iterator ait;


	namestream << "__stub_" << f->getName().str();
	m = km->module;
	newname = namestream.str();
	ret_f = m->getFunction(newname);
	if (ret_f != NULL) return ret_f;

	std::vector<Type *> argTypes;

	argTypes.push_back(arg0->getType());
	if (arg1) argTypes.push_back(arg1->getType());
	ft = FunctionType::get(retType, argTypes, false);

	ret_f = Function::Create(ft, GlobalValue::ExternalLinkage, newname, m);
	bb = BasicBlock::Create(getGlobalContext(), "entry", ret_f);

	ait = ret_f->getArgumentList().begin();
	arg_a = &*ait;
	arg_a->setName("a");

	cast_type = IntegerType::get(
		getGlobalContext(),
		f->getArgumentList().front().getType()->
			getPrimitiveSizeInBits());

	v_arg0 = CastInst::CreateZExtOrBitCast(arg_a, cast_type, "", bb);

	if (arg1) {
		arg_b = (arg1) ? &*++ait : NULL;
		arg_b->setName("b");
		v_arg1 = CastInst::CreateZExtOrBitCast(arg_b, cast_type, "", bb);
	}

	std::vector<Value*>	callargs;

	callargs.push_back(v_arg0);
	if (arg1) callargs.push_back(v_arg1);

	v_ret = CallInst::Create(f, callargs, "", bb);
	v_ret = CastInst::CreateTruncOrBitCast(v_ret, retType, "", bb);
	ReturnInst::Create(getGlobalContext(), v_ret, bb);
	km->addFunctionProcessed(ret_f);

	return ret_f;
}

bool SoftFPPass::replaceFCmp(Instruction *inst)
{
	IRBuilder<>	irb(inst);
	FCmpInst 	*fi;
	CmpInst::Predicate pred;
	unsigned	ty_w;
	Value		*v, *v0, *arg[2];
	Type		*ty, *ret_type;
	Function	*f;
	bool		f_flip, unordered = false;

	ty = inst->getType();
	v0 = (inst->getNumOperands() > 0) ? inst->getOperand(0) : NULL;
	ty_w = v0->getType()->getPrimitiveSizeInBits();

	assert (ty_w == 32 || ty_w == 64);

	fi = cast<FCmpInst>(inst);

	std::cerr << "[FP] GET NANs working right\n";

	// Ordered comps return false if either operand is NaN.
	// Unordered comps return true if either operand is NaN.
	pred = fi->getPredicate();
	f_flip = false;
	/* Ordered AND cond */
#define GET_F(x)	\
	f = (ty_w == 32) ? f_fp32##x->function : f_fp64##x->function
	if (pred == FCmpInst::FCMP_OEQ) {
		GET_F(eq);
	} else if (pred == FCmpInst::FCMP_OLT) {
		GET_F(lt);
	}  else if (pred == FCmpInst::FCMP_OLE) {
		GET_F(le);
		f_flip = true;
	} else if (pred == FCmpInst::FCMP_OGT) {
		GET_F(lt);
		f_flip = true;
	}
	/* Unordered OR cond */
	else if (pred == FCmpInst::FCMP_UEQ) {
		GET_F(eq);
		unordered = true;
	} else if (pred == FCmpInst::FCMP_ULT) {
		GET_F(lt);
		unordered = true;
	}  else if (pred == FCmpInst::FCMP_ULE) {
		GET_F(le);
		f_flip = true;
		unordered = true;
	} else if (pred == FCmpInst::FCMP_UGT) {
		GET_F(lt);
		f_flip = true;
		unordered = true;
	} else {
		inst->dump();
		assert (0 == 1 && "???");
	}
#undef GET_F

	if (f_flip) {
		arg[1] = v0;
		arg[0] = inst->getOperand(1);
	} else {
		arg[0] = v0;
		arg[1] = inst->getOperand(1);
	}

	ret_type = IntegerType::get(ty->getContext(), 1);

	v = CallInst::Create(getCastThunk(f, ret_type, arg[0], arg[1]), arg);
	std::cerr << "URB DONE\n";

	if (unordered) {
		Function	*is_nan;

		std::cerr << "UNORDERD!!!\n";
		is_nan = (ty_w == 32)
			? f_fp32isnan->function
			: f_fp64isnan->function;
		v = irb.CreateOr(
			irb.CreateCall(
				getCastThunk(is_nan, ret_type, v0, NULL),
				v0),
			v);
	}

	std::cerr << "REPLACEING\n";
	ReplaceInstWithInst(inst, static_cast<Instruction*>(v));
	return true;

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
		fp_call = CallInst::Create(				\
			getCastThunk(f, ty, args[0], args[1]),		\
			ArrayRef<Value*>(args, 2));	\
		ReplaceInstWithInst(inst, fp_call);	\
		return true; }

	OP_REPL(Add, add)
	OP_REPL(Sub, sub)
	OP_REPL(Mul, mul)
	OP_REPL(Div, div)

	case Instruction::FCmp: return replaceFCmp(inst);

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

	case Instruction::Call: {
		unsigned	ty_w = v0->getType()->getPrimitiveSizeInBits();
		CallInst	*ci = static_cast<CallInst*>(inst);
		Function	*called;

		called = ci->getCalledFunction();
		if (called == NULL)
			break;

		if (called->getName().str() == "sqrt") {
			Function	*f;
			f = (ty_w == 32)
				? f_fp32sqrt->function
				: f_fp64sqrt->function;
			ReplaceInstWithInst(inst, CallInst::Create(f, v0));
		}
	}
	break;

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