//===-- KInstruction.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/Support/CallSite.h>
#include <llvm/IR/DataLayout.h>
#include <algorithm>
#include <string.h>
#include <math.h>

#include "../Core/Executor.h"
#include "../Core/Context.h"
#include "klee/Expr.h"
#include "klee/util/GetElementPtrTypeIterator.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/KModule.h"
#include "static/Sugar.h"

using namespace llvm;
using namespace klee;

KBrInstruction::kbr_list_ty 	KBrInstruction::all_kbr;
unsigned			KBrInstruction::kbr_c = 0;

KInstruction::~KInstruction() { delete[] operands; }

KInstruction::KInstruction(Instruction* in_inst, unsigned in_dest)
: inst(in_inst)
, info(0)
, fork_c(0)
, dest(in_dest)
, cover_sid(~0UL)
{
	if (isCall()) {
		/* [0] = getCalledValue() */
		/* [1...] = args */
		operands = new int[getNumArgs()+1];
		memset(operands, 0xff, sizeof(int)*(getNumArgs()+1));
		return;
	}

	operands = new int[getNumArgs()];
	memset(operands, 0xff, sizeof(int)*getNumArgs());
}

unsigned KInstruction::getNumArgs(void) const
{
	if (isCall()) {
		CallSite	cs(inst);
		return cs.arg_size();
	}

	return inst->getNumOperands();
}

bool KInstruction::isCall(void) const
{ return (isa<CallInst>(inst) || isa<InvokeInst>(inst)); }


KInstruction* KInstruction::create(
	KModule* km, llvm::Instruction* inst, unsigned dest)
{
	switch(inst->getOpcode()) {
	case Instruction::GetElementPtr:
	case Instruction::InsertValue:
	case Instruction::ExtractValue:
		return new KGEPInstruction(km, inst, dest);
	case Instruction::Br:
		return new KBrInstruction(inst, dest);
	case Instruction::Switch:
		return new KSwitchInstruction(inst, dest);
	default:
		return new KInstruction(inst, dest);
	}
}

KGEPInstruction::KGEPInstruction(
	KModule* km,
	llvm::Instruction* inst, unsigned dest)
: KInstruction(inst, dest)
, offset(KGEP_OFF_UNINIT)
{}

void KGEPInstruction::resolveConstants(const KModule* km, const Globals* g)
{
	if (GetElementPtrInst *gepi = dyn_cast<GetElementPtrInst>(getInst())) {
		computeOffsets(km, g, gep_type_begin(gepi), gep_type_end(gepi));
	} else if (InsertValueInst *ivi = dyn_cast<InsertValueInst>(getInst())) {
		computeOffsets(km, g, iv_type_begin(ivi), iv_type_end(ivi));
		assert(	indices.empty() &&
			"InsertValue constant offset expected");
	} else if (ExtractValueInst *evi = dyn_cast<ExtractValueInst>(getInst())) {
		computeOffsets(km, g, ev_type_begin(evi), ev_type_end(evi));
		assert(	indices.empty() &&
			"ExtractValue constant offset expected");
	} else {
		assert (0 == 1 && "UNKNOWN GEPI");
	}
}

template <typename TypeIt>
void KGEPInstruction::computeOffsets(
	const KModule* km, const Globals* g, TypeIt ib, TypeIt ie)
{
	ref<ConstantExpr>	constantOffset;
	uint64_t		index;

	indices.clear();

	constantOffset = MK_CONST(0, Context::get().getPointerWidth());

	index = 1;
	for (TypeIt ii = ib; ii != ie; ++ii) {
	if (StructType *st = dyn_cast<StructType>(*ii)) {
		const StructLayout	*sl;
		const ConstantInt	*ci;
		uint64_t		addend;

		sl = km->dataLayout->getStructLayout(st);
		ci = cast<ConstantInt>(ii.getOperand());
		addend = sl->getElementOffset((unsigned) ci->getZExtValue());

		constantOffset = constantOffset->Add(
			MK_CONST(addend, Context::get().getPointerWidth()));
	} else {
		const SequentialType	*st2;
		uint64_t		elementSize;
		Value			*operand;

		st2 = cast<SequentialType>(*ii);
		elementSize = km->dataLayout->getTypeStoreSize(
			st2->getElementType());
		operand = ii.getOperand();

		if (Constant *c = dyn_cast<Constant>(operand)) {
			ref<ConstantExpr>	cVal;
			ref<ConstantExpr>	index;
			ref<ConstantExpr>	addend;

			cVal = Executor::evalConstant(km, g, c);

			index = cast<ConstantExpr>(cVal)->SExt(
				Context::get().getPointerWidth());

			addend = index->Mul(
				MK_CONST(
					elementSize,
					Context::get().getPointerWidth()));
			constantOffset = constantOffset->Add(addend);

		} else {
			indices.push_back(std::make_pair(index, elementSize));
		}
	}
	index++;
	}

	offset = constantOffset->getZExtValue();
}


/* deterministically order basic blocks by lowest value */
void KSwitchInstruction::orderTargets(const KModule* km, const Globals* g)
{
	SwitchInst	*si(cast<SwitchInst>(getInst()));
	unsigned	i;

	/* already ordered targets! */
	if (cases.size())
		return;

	cases.resize(si->getNumCases()+1);
	assert (cases.size() >= 2);

	/* initialize minTargetValues and cases */
	cases[0] = Val2TargetTy(ref<ConstantExpr>(), si->getDefaultDest());
	assert (si->getDefaultDest() == si->case_default().getCaseSuccessor());

	foreach (it, si->case_begin(), si->case_end()) {
		ref<ConstantExpr>	value;
		BasicBlock		*target;
		TargetValsTy::iterator	it2;

		i = it.getCaseIndex();
		assert ((i+1) < cases.size());

		value  = Executor::evalConstant(km, g, it.getCaseValue());
		target = it.getCaseSuccessor();
		cases[i+1] = Val2TargetTy(value, target);

		it2 = minTargetValues.find(target);
		if (it2 == minTargetValues.end())
			minTargetValues[target] = value;
		else if (value < it2->second)
			it2->second = value;
	}

	// build map from target BasicBlock to value(s) that lead to that block
	for (unsigned i = 1; i < cases.size(); ++i)
		caseMap[cases[i].second].insert(cases[i].first->getZExtValue());
}

#define EXE_SWITCH_RLE_LIMIT	4

TargetTy KSwitchInstruction::getExprCondSwitchTargets(
	ref<Expr> cond, TargetsTy& targets)
{
	ref<Expr>	defCase;

	defCase = ConstantExpr::create(1, Expr::Bool);

	// generate conditions for each block
	foreach (cit, caseMap.begin(), caseMap.end()) {
		BasicBlock *target = cit->first;
		std::set<uint64_t> &values = cit->second;
		ref<Expr> match = ConstantExpr::create(0, Expr::Bool);

		// try run-length encoding long sequences of consecutive
		// switch values that map to the same BasicBlock
		foreach (vit, values.begin(), values.end()) {
			std::set<uint64_t>::iterator vit2 = vit;
			uint64_t runLen = 1;

			for (++vit2; vit2 != values.end(); ++vit2) {
				if (*vit2 != *vit + runLen)
					break;
				runLen++;
			}

			if (runLen < EXE_SWITCH_RLE_LIMIT) {
				match = MK_OR(
					match,
					MK_EQ(	cond,
						MK_CONST(*vit,
						cond->getWidth())));
				continue;
			}

			// use run-length encoding
			ref<Expr>	rle_bounds;
			rle_bounds = AndExpr::create(
				MK_UGE(cond, MK_CONST(*vit, cond->getWidth())),
				MK_ULT(cond, MK_CONST(
					*vit + runLen, cond->getWidth())));

			match = OrExpr::create(match, rle_bounds);

			vit = vit2;
			--vit;
		}

		targets.insert(std::make_pair(
			(minTargetValues.find(target))->second,
			std::make_pair(target, match)));

		// default case is the AND of all the complements
		defCase = MK_AND(defCase, Expr::createIsZero(match));
	}

	// include default case
	return std::make_pair(cases[0].second, defCase);
}

// Somewhat gross to create these all the time, but fine till we
// switch to an internal rep.
TargetTy KSwitchInstruction::getConstCondSwitchTargets(
	uint64_t v, TargetsTy &targets)
{
	SwitchInst		*si;
	llvm::IntegerType	*Ty;
	ConstantInt		*ci;
	int			index;
	TargetTy		defaultTarget;
	TargetsConstTy::iterator	it;

	/* memoized because it's 5% runtime for concrete python. yuck */
	it = const_defaults.find(v);
	if (it != const_defaults.end()) {
		targets = it->second.second;
		return it->second.first;
	}

	targets.clear();

	si = cast<SwitchInst>(getInst());
	Ty = cast<IntegerType>(si->getCondition()->getType());
	ci = ConstantInt::get(Ty, v);

	SwitchInst::CaseIt	cit(si->findCaseValue(ci));
	index = (cit == si->case_end())
		? -1
		: cit.getCaseIndex();

	// We need to have the same set of targets to pass to fork() in case
	// toUnique fails/times out on replay (it's happened before...)
	defaultTarget = TargetTy(cases[0].second, MK_CONST(0, Expr::Bool));
	if (index < 0)
		defaultTarget.second = MK_CONST(1, Expr::Bool);

	for (int i = 1; i < (int)cases.size(); ++i) {
		// default to infeasible target
		TargetsTy::iterator	it;
		TargetTy		cur_target;

		cur_target = TargetTy(cases[i].second, MK_CONST(0, Expr::Bool));
		it = targets.insert(
			std::make_pair(
				minTargetValues.find(cases[i].second)->second,
				cur_target)).first;

		// set unique target as feasible
		if (i-1 == index) {
			it->second.second = MK_CONST(1, Expr::Bool);
		}
	}

	/* rate limit */
	if (const_defaults.size() > 256)
		const_defaults.clear();

	const_defaults[v] = std::make_pair(defaultTarget, targets);
	return defaultTarget;
}

double KBrInstruction::getForkMean(void)
{
	unsigned	i;
	unsigned	forks;

	i = 0;
	forks = 0;
	foreach (it, all_kbr.begin(), all_kbr.end()) {
		unsigned	hits((*it)->getForkHits());

		if (!hits) continue;
		forks += hits;
		i++;
	}

	if (i == 0)
		return 0;

	return (double)forks / (double)i;
}

double KBrInstruction::getForkStdDev(void)
{
	unsigned	i;
	unsigned	forks, forks_sq;
	double		mean_sq, sum_sq;

	i = 0;
	forks = 0;
	forks_sq = 0;
	foreach (it, all_kbr.begin(), all_kbr.end()) {
		unsigned	hits((*it)->getForkHits());
		if (!hits) continue;
		forks += hits;
		forks_sq += hits*hits;
		i++;
	}

	if (i == 0)
		return 0;

	mean_sq = (double)forks / (double)i;
	mean_sq *= mean_sq;

	sum_sq = (double)forks_sq / (double)i;

	return sqrt(sum_sq - mean_sq);
}

double KBrInstruction::getForkMedian(void)
{
	std::vector<unsigned> forks_v;

	foreach (it, all_kbr.begin(), all_kbr.end()) {
		unsigned	hits((*it)->getForkHits());
		if (!hits) continue;
		forks_v.push_back(hits);
	}

	if (forks_v.size() == 0)
		return 1;

	std::sort(forks_v.begin(), forks_v.end());
	return forks_v[forks_v.size()/2];
}

Function* KInstruction::getFunction(void) const
{ return inst->getParent()->getParent(); }
