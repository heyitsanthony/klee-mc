#include "klee/Internal/Module/KModule.h"
#include "../lib/Core/Context.h"

#include "klee/Common.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/InstructionInfoTable.h"

#include <llvm/IR/CallSite.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

#include <iostream>

#include "static/Sugar.h"

using namespace klee;
using namespace llvm;

unsigned KFunction::inst_clock = 0;

static int getOperandNum(
	Value *v,
        const std::map<Instruction*, unsigned> &regMap,
        KModule *km,
        KInstruction *ki)
{
	if (Instruction *inst = dyn_cast<Instruction>(v))
		return regMap.find(inst)->second;

	if (Argument *a = dyn_cast<Argument>(v))
		return a->getArgNo();

	if (isa<BasicBlock>(v) || isa<InlineAsm>(v)) // || isa<MDNode>(v))
		return -1;

	assert(isa<Constant>(v));
	Constant *c = cast<Constant>(v);
	return -(km->getConstantID(c, ki) + 2);
}

KFunction::KFunction(llvm::Function *_function, KModule *km)
: function(_function)
, numArgs(function->arg_size())
, numInstructions(0)
, callcount(0)
, instructions(0)
, arguments(0)
, trackCoverage(true)
, pathCommitted(false)
, isSpecial(false)
, enter_c(0)
, exit_c(0)
, mod_name(NULL)
, path_commit_tick(~0)
{
	std::map<Instruction*, unsigned> regMap;
	unsigned arg_c = 0;
	unsigned rnum = numArgs;
	unsigned ins_num = 0;

	inst_tick = inst_clock++;

	foreach (bbit, function->begin(), function->end()) {
		BasicBlock *bb = bbit;
		basicBlockEntry[bb] = numInstructions;
		numInstructions += bb->size();
	}

	arguments = new Value*[numArgs];
	foreach (it, function->arg_begin(), function->arg_end()) {
		Value* v = &*it;
		arguments[arg_c++] = v;
	}

	// The first arg_size() registers are reserved for formals.
	foreach (bbit, function->begin(), function->end()) {
		foreach (it, bbit->begin(), bbit->end()) {
			regMap[it] = rnum++;
		}
	}
	numRegisters = rnum;

	// build shadow instructions */
	instructions = new KInstruction*[numInstructions+1];
	instructions[numInstructions] = NULL;
	foreach (bbit, function->begin(), function->end()) {
		foreach(it, bbit->begin(), bbit->end()) {
			addInstruction(km, it, regMap, ins_num);
			ins_num++;
		}
	}
}

void KFunction::addInstruction(
	KModule	*km,
	llvm::Instruction* inst,
	const std::map<llvm::Instruction*, unsigned>& regMap,
	unsigned int ins_num)
{
	KInstruction	*ki;
	unsigned	numArgs;

	ki = KInstruction::create(km, inst, regMap.find(inst)->second);
	instructions[ins_num] = ki;

	numArgs = ki->getNumArgs();

	if (ki->isCall()) {
		CallSite	cs(inst);

		ki->setOperand(
			0, getOperandNum(cs.getCalledValue(), regMap, km, ki));
		for (unsigned j=0; j < numArgs; j++) {
			Value *v = cs.getArgument(j);
			ki->setOperand(j+1, getOperandNum(v, regMap, km, ki));
		}
		return;
	}

	for (unsigned j=0; j < numArgs; j++) {
		Value *v = inst->getOperand(j);
		int op_num = getOperandNum(v, regMap, km, ki);
		ki->setOperand(j, op_num);
	}
}

llvm::Value* KFunction::getValueForRegister(unsigned reg)
{
	if (reg < numArgs)
		return arguments[reg];

	return instructions[reg - numArgs]->getInst();
}

KFunction::~KFunction()
{
	for (unsigned i=0; i<numInstructions; ++i)
		delete instructions[i];

	delete[] arguments;
	delete[] instructions;
}

unsigned KFunction::getUncov(void) const
{
	unsigned ret = 0;

	/* first instruction dominates all instructions in function */
	if (instructions[0]->isCovered() == false)
		return numInstructions;

	for (unsigned i = 0; i < numInstructions; i++)
		if (instructions[i]->isCovered() == false)
			ret++;
	return ret;
}

unsigned KFunction::getCov(void) const { return numInstructions - getUncov(); }

static const char tohex[] = "0123456789abcdef";
std::string KFunction::getCovStr(void) const
{
	/* one character for every four bits */
	std::string		ret("");
	std::vector<unsigned>	v((numInstructions + 3)/4, 0);

	for (unsigned i = 0; i < numInstructions; i++) {
		v[i/4] <<= 1;
		v[i/4] |= (instructions[i]->isCovered() ? 1 : 0);
	}

	for (unsigned i = 0; i < v.size(); i++)
		ret += tohex[v[i]];

	return ret;
}
