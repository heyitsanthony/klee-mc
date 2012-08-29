//===-- KInstruction.h ------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_KINSTRUCTION_H
#define KLEE_KINSTRUCTION_H

#include "klee/util/ExprHashMap.h"
#include <llvm/Support/DataTypes.h>
#include <vector>
#include <list>
#include <assert.h>

namespace llvm {
	class Instruction;
	class BasicBlock;
	class Function;
}

namespace klee
{
class Globals;
class Executor;
struct InstructionInfo;
class KModule;

/// KInstruction - Intermediate instruction representation used
/// during execution.
class KInstruction
{
public:
	virtual ~KInstruction();
	llvm::Instruction* getInst(void) const { return inst; }
	static KInstruction* create(
		KModule* km,
		llvm::Instruction* inst, unsigned dest);
	unsigned getNumArgs(void) const;
	bool isCall(void) const;
	void setOperand(unsigned op_num, int n) { operands[op_num] = n; }
	int getOperand(unsigned op_num) const { return operands[op_num]; }
	unsigned getDest(void) const { return dest; }
	void setInfo(const InstructionInfo* inf) { info = inf; }
	const InstructionInfo* getInfo(void) const { return info; }

	bool isCovered(void) const { return covered; }
	void cover(void) { covered = true; }

	llvm::Function* getFunction(void) const;

	unsigned getForkCount(void) const { return fork_c; }
	void forked(void) { fork_c++; }
protected:
	KInstruction() : covered(false) {}
	KInstruction(llvm::Instruction* inst, unsigned dest);

private:
	llvm::Instruction	*inst;
	const InstructionInfo	*info;

	/* number of forks on this instruction */
	unsigned		fork_c;

	/// Value numbers for each operand. -1 is an invalid value,
	/// otherwise negative numbers are indices (negated and offset by
	/// 2) into the module constant table and positive numbers are
	/// register indices.
	int			*operands;

	/// Destination register index.
	unsigned		dest;
	bool			covered;
};

typedef std::pair<llvm::BasicBlock*, ref<Expr> >	TargetTy;
typedef std::map<ref<ConstantExpr>, TargetTy >		TargetsTy;
typedef std::map<llvm::BasicBlock*, ref<ConstantExpr> >	TargetValsTy;
typedef std::pair<ref<ConstantExpr>, llvm::BasicBlock*>	Val2TargetTy;

class KSwitchInstruction : public KInstruction
{
public:
	KSwitchInstruction(llvm::Instruction* ins, unsigned dest)
	: KInstruction(ins, dest)
	{}
	virtual ~KSwitchInstruction() {}

	void orderTargets(const KModule* km, const Globals* g);

	TargetTy getConstCondSwitchTargets(uint64_t v, TargetsTy &t);
	TargetTy getExprCondSwitchTargets(ref<Expr> cond, TargetsTy &t);
private:
	std::vector<Val2TargetTy >	cases;
	TargetValsTy			minTargetValues; // lowest val -> BB
	std::map<llvm::BasicBlock*, std::set<uint64_t> > caseMap;
};

/* potassium bromide instructoin */
class KBrInstruction : public KInstruction
{
public:
	typedef std::list<KBrInstruction*> kbr_list_ty;

	KBrInstruction(llvm::Instruction* ins, unsigned dest)
	: KInstruction(ins, dest)
	, seen_expr_c(0)
	{
		all_kbr.push_back(this);
		kbr_c++;
	}

	virtual ~KBrInstruction() {}

	bool hasFoundAll(void) const { return br_true.hits && br_false.hits; }
	void foundTrue(uint64_t inst_clock) { foundBr(br_true, inst_clock); }
	void foundFalse(uint64_t inst_clock) { foundBr(br_false, inst_clock); }
	void foundFork(uint64_t inst_clock) { foundBr(br_fork, inst_clock); }
	bool hasFoundTrue(void) const { return br_true.hits != 0; }
	bool hasFoundFalse(void) const { return br_false.hits != 0; }
	unsigned getTrueHits(void) const { return br_true.hits; }
	unsigned getFalseHits(void) const { return br_false.hits; }

	void followedTrue(void) { br_true.follows++; }
	void followedFalse(void) { br_false.follows++;}

	unsigned getTrueFollows(void) const { return br_true.follows; }
	unsigned getFalseFollows(void) const { return br_false.follows; }

	uint64_t getTrueMinInst(void) const { return br_true.min_inst; }
	uint64_t getFalseMinInst(void) const { return br_false.min_inst; }
	unsigned getForkHits(void) const { return br_fork.hits; }


	void addExpr(const ref<Expr>& e) { expr_set.insert(e); }
	void seenExpr(void) { seen_expr_c++; }
	bool hasSeenExpr(void) const { return seen_expr_c != 0; }
	unsigned getSeenExprs(void) const { return seen_expr_c; }

	static kbr_list_ty::const_iterator beginBr()
	{ return all_kbr.begin(); }
	static kbr_list_ty::const_iterator endBr(void)
	{ return all_kbr.end(); }

	static double getForkMean(void);
	static double getForkStdDev(void);
	static double getForkMedian(void);
private:
	struct BrType {
		BrType()
		: hits(0), min_inst(~0), max_inst(0), follows(0) {}
		unsigned	hits;
		uint64_t	min_inst;
		uint64_t	max_inst;
		unsigned	follows;
	};

	void foundBr(BrType& brt, uint64_t inst_clock)
	{
		brt.hits++;
		if (brt.min_inst > inst_clock)
			brt.min_inst = inst_clock;
		if (brt.max_inst < inst_clock)
			brt.max_inst = inst_clock;
	}

	BrType	br_true;
	BrType	br_false;
	BrType	br_fork;

	unsigned		seen_expr_c;
	ExprHashSet		expr_set;

	static kbr_list_ty	all_kbr;
	static unsigned		kbr_c;
};

#define KGEP_OFF_UNINIT	0xdeadbeef12345678
class KGEPInstruction : public KInstruction
{
public:
	KGEPInstruction(
		KModule* km, llvm::Instruction* inst, unsigned dest);

	virtual ~KGEPInstruction() {}
	/// indices - The list of variable sized adjustments to add to the pointer
	/// operand to execute the instruction. The first element is the operand
	/// index into the GetElementPtr instruction, and the second element is the
	/// element size to multiple that index by.
	std::vector< std::pair<unsigned, uint64_t> > indices;

	uint64_t getOffsetBits(void) const
	{ assert (offset != KGEP_OFF_UNINIT); return offset; }

	void resolveConstants(const KModule* km, const Globals* gs);
private:
	template <typename TypeIt>
	void computeOffsets(
		const KModule* km, const Globals*, TypeIt ib, TypeIt ie);

	/// offset - A constant offset to add to the pointer operand to execute the
	/// insturction.
	uint64_t offset;
};
}

#endif
