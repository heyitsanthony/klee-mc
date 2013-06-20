#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Support/Path.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Support/CommandLine.h>
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

namespace
{
	llvm::cl::opt<std::string>
	SoftFPLib(
		"softfp-lib",
		llvm::cl::desc("Soft FPU library file."),
		llvm::cl::init("softfloat-fpu.bc"));
}

namespace klee { extern Module* getBitcodeModule(const char* path); }

char SoftFPPass::ID;

SoftFPPass::SoftFPPass(KModule* _km)
: llvm::FunctionPass(ID)
, km(_km)
{
	llvm::Module	*mod;
	llvm::sys::Path	path(km->getLibraryDir());

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

	path.appendComponent(SoftFPLib.c_str());
	std::cerr << "[SoftFPU] Using library '" << path.c_str() << "'\n";


	mod = getBitcodeModule(path.c_str());
	assert (mod != NULL);

	km->addModule(mod);

	/* store all softfp functions into object's fields */
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
	Function		*ret_f;
	BasicBlock		*bb;
	Type			*cast_type;
	std::stringstream	namestream;
	std::string		newname;
	Value			*v_ret, *v_arg0, *v_arg1;

	assert (f != NULL);

	namestream << "__stub_" << f->getName().str();
	newname = namestream.str();

	ret_f = km->module->getFunction(newname);
	if (ret_f != NULL) return ret_f;

	bb = setupFuncEntry(newname, retType, arg0, arg1, &v_arg0, &v_arg1);
	ret_f = bb->getParent();

	cast_type = IntegerType::get(
		getGlobalContext(),
		f->getArgumentList().front().getType()->
			getPrimitiveSizeInBits());

	v_arg0 = CastInst::CreateZExtOrBitCast(v_arg0, cast_type, "", bb);
	if (arg1)
		v_arg1 = CastInst::CreateZExtOrBitCast(
			v_arg1, cast_type, "", bb);

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
	FCmpInst 	*fi;
	CmpInst::Predicate pred;
	unsigned	ty_w;
	Value		*v, *v0, *arg[2];
	Type		*ty;
	Function	*f;
	bool		f_flip, ordered;

	ty = inst->getType();
	v0 = (inst->getNumOperands() > 0) ? inst->getOperand(0) : NULL;
	ty_w = v0->getType()->getPrimitiveSizeInBits();

	assert (ty_w == 32 || ty_w == 64);

	fi = cast<FCmpInst>(inst);

	std::cerr << "[FP] GET NANs working right\n";

	// Ordered comps return false if either operand is NaN.
	// Unordered comps return true if either operand is NaN.
	pred = fi->getPredicate();
	ordered = true;
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
	} else if (pred == FCmpInst::FCMP_OGT) {
		GET_F(le);
		f_flip = true;
	} else if (pred == FCmpInst::FCMP_OGE) {
		GET_F(lt);
		f_flip = true;
	}
	/* Unordered OR cond */
	else if (pred == FCmpInst::FCMP_UEQ) {
		GET_F(eq);
		ordered = false;
	} else if (pred == FCmpInst::FCMP_ULT) {
		GET_F(lt);
		ordered = false;
	}  else if (pred == FCmpInst::FCMP_ULE) {
		GET_F(le);
		ordered = false;
	} else if (pred == FCmpInst::FCMP_UGT) {
		GET_F(le);
		f_flip = true;
		ordered = false;
	} else if (pred == FCmpInst::FCMP_UGE) {
		GET_F(lt);
		f_flip = true;
		ordered = false;
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

	if (ordered) {
		/* vexops runtime calls only ORDERED comparisons */
		v = CallInst::Create(
			getOrderedStub(f, arg[0], arg[1]),
			ArrayRef<Value*>(arg, 2));
	} else {
		v = CallInst::Create(
			getUnorderedStub(f, arg[0], arg[1]),
			ArrayRef<Value*>(arg, 2));

	}

	ReplaceInstWithInst(inst, static_cast<Instruction*>(v));
	return true;
}



Function* SoftFPPass::getOrderedStub(
	Function* f, Value* arg0, Value* arg1)
{
	Function		*ret_f, *thunk_f;
	BasicBlock		*bb;
	Function		*is_nan;
	Value			*v_arg[2], *v_cmp, *v_nan[2], *v;
	Type			*ret_type;
	unsigned		ty_w;
	std::stringstream	namestream;
	std::string		newname;

	namestream << "__stub_O_" << f->getName().str();
	newname = namestream.str();

	ret_f = km->module->getFunction(newname);
	if (ret_f != NULL) return ret_f;

	ret_type = IntegerType::get(getGlobalContext(), 1);
	bb = setupFuncEntry(
		newname, ret_type, arg0, arg1, &v_arg[0], &v_arg[1]);

	ret_f = bb->getParent();

	ty_w = arg0->getType()->getPrimitiveSizeInBits();
	is_nan = (ty_w == 32)
		? f_fp32isnan->function
		: f_fp64isnan->function;

	/* TODO: proper 'or' short circuiting */
	v_nan[0] = CallInst::Create(
		getCastThunk(is_nan, ret_type, v_arg[0], NULL),
		ArrayRef<Value*>(&v_arg[0],1),
		"",
		bb);

	v_nan[1] = CallInst::Create(
		getCastThunk(is_nan, ret_type, v_arg[1], NULL),
		ArrayRef<Value*>(&v_arg[1],1),
		"",
		bb);

	thunk_f = getCastThunk(f, ret_type, v_arg[0], v_arg[1]);
	assert (thunk_f != NULL);
	v_cmp = CallInst::Create(thunk_f, ArrayRef<Value*>(v_arg, 2), "", bb);

	/* (!(is_nan(a) || is_nan(b)) || a OP b) */
	v = BinaryOperator::Create(
		BinaryOperator::Or, v_nan[0], v_nan[1], "", bb);
	v = BinaryOperator::CreateNot(v, "", bb);
	v = BinaryOperator::Create(BinaryOperator::And, v, v_cmp, "", bb);

	ReturnInst::Create(getGlobalContext(), v, bb);
	km->addFunctionProcessed(ret_f);

	assert (ret_f != NULL);
	return ret_f;
}

Function* SoftFPPass::getUnorderedStub(
	Function* f, Value* arg0, Value* arg1)
{
	Function		*ret_f, *thunk_f;
	BasicBlock		*bb;
	Function		*is_nan;
	Value			*v_arg[2], *v_cmp, *v_nan[2], *v;
	Type			*ret_type;
	unsigned		ty_w;
	std::stringstream	namestream;
	std::string		newname;

	namestream << "__stub_UO_" << f->getName().str();
	newname = namestream.str();

	ret_f = km->module->getFunction(newname);
	if (ret_f != NULL) return ret_f;

	ret_type = IntegerType::get(getGlobalContext(), 1);
	bb = setupFuncEntry(
		newname, ret_type, arg0, arg1, &v_arg[0], &v_arg[1]);

	ret_f = bb->getParent();

	ty_w = arg0->getType()->getPrimitiveSizeInBits();
	is_nan = (ty_w == 32)
		? f_fp32isnan->function
		: f_fp64isnan->function;

	/* TODO: proper 'or' short circuiting */
	v_nan[0] = CallInst::Create(
		getCastThunk(is_nan, ret_type, v_arg[0], NULL),
		ArrayRef<Value*>(&v_arg[0],1),
		"",
		bb);

	v_nan[1] = CallInst::Create(
		getCastThunk(is_nan, ret_type, v_arg[1], NULL),
		ArrayRef<Value*>(&v_arg[1],1),
		"",
		bb);

	thunk_f = getCastThunk(f, ret_type, v_arg[0], v_arg[1]);
	assert (thunk_f != NULL);
	v_cmp = CallInst::Create(thunk_f, ArrayRef<Value*>(v_arg, 2), "", bb);

	v = BinaryOperator::Create(
		BinaryOperator::Or, v_nan[0], v_nan[1], "", bb);
	v = BinaryOperator::CreateNot(v, "", bb);
	v = BinaryOperator::Create(BinaryOperator::And, v, v_cmp, "", bb);

	ReturnInst::Create(getGlobalContext(), v, bb);
	km->addFunctionProcessed(ret_f);

	assert (ret_f != NULL);
	return ret_f;
}


BasicBlock* SoftFPPass::setupFuncEntry(
	const std::string& newname,
	Type* retType,
	Value* arg0, Value* arg1,
	Value** v_arg0, Value** v_arg1)
{
	Function		*ret_f;
	FunctionType		*ft;
	BasicBlock		*bb;
	Argument		*arg_a, *arg_b;
	std::vector<Type *>	argTypes;
	Function::ArgumentListType::iterator ait;

	argTypes.push_back(arg0->getType());
	if (arg1) argTypes.push_back(arg1->getType());
	ft = FunctionType::get(retType, argTypes, false);

	ret_f = Function::Create(
		ft,
		GlobalValue::ExternalLinkage,
		newname,
		km->module);
	bb = BasicBlock::Create(getGlobalContext(), "entry", ret_f);

	ait = ret_f->getArgumentList().begin();
	arg_a = &*ait;
	arg_a->setName("a");
	*v_arg0 = arg_a;

	if (arg1) {
		arg_b = (arg1) ? &*++ait : NULL;
		arg_b->setName("b");
		*v_arg1 = arg_b;
	} else
		*v_arg1 = NULL;
	return bb;
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

		fpext_call = CallInst::Create(
			getCastThunk(f_fpext->function, ty, v0), v0);
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
	OP_REPL(Rem, rem)

	case Instruction::FCmp: return replaceFCmp(inst);

	case Instruction::FPToUI:
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

		fp_call = CallInst::Create(getCastThunk(f, ty, v0), v0);
		ReplaceInstWithInst(inst, fp_call);
		return true;
	}

	case Instruction::UIToFP:
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

		fp_call = CallInst::Create(getCastThunk(f, ty, v0), v0);
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
		fp_call = CallInst::Create(getCastThunk(f, ty, v0), v0);
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

		if (called->getName().str() == "sqrtf") {
			Function	*f;
			assert (ty_w == 32);
			f = f_fp32sqrt->function;
			ReplaceInstWithInst(
				inst,
				CallInst::Create(
					getCastThunk(f, ty, v0), v0));
		} else 	if (called->getName().str() == "sqrt") {
			Function	*f;
			assert (ty_w == 64);
			f = f_fp64sqrt->function;
			ReplaceInstWithInst(
				inst,
				CallInst::Create(
					getCastThunk(f, ty, v0), v0));
		}
	}
	break;

	default:
#if 0
		std::cerr << "UNKNOWN INST: ";
		inst->dump();
		std::cerr << "\n=======FUNC======\n";
		inst->getParent()->getParent()->dump();
		assert (0 == 1 && "IMPLEMENT ME!!!!!!");
#endif
	break;
	}

	return false;
}