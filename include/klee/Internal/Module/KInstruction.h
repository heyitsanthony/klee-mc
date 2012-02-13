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

#include <llvm/Support/DataTypes.h>
#include <vector>

namespace llvm {
	class Instruction;
}

namespace klee {
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

protected:
	KInstruction() : covered(false) {}
	KInstruction(llvm::Instruction* inst, unsigned dest);

private:
	llvm::Instruction	*inst;
	const InstructionInfo	*info;

	/// Value numbers for each operand. -1 is an invalid value,
	/// otherwise negative numbers are indices (negated and offset by
	/// 2) into the module constant table and positive numbers are
	/// register indices.
	int			*operands;

	/// Destination register index.
	unsigned		dest;

	bool			covered;
};

/* potassium bromide instructoin */
class KBrInstruction : public KInstruction
{
public:
	KBrInstruction(llvm::Instruction* ins, unsigned dest)
	: KInstruction(ins, dest)
	, foundTrue(false)
	, foundFalse(false) {}
	virtual ~KBrInstruction() {}

	bool foundAll(void) const { return foundTrue && foundFalse; }
	void setFoundTrue(void) { foundTrue = true; }
	void setFoundFalse(void) { foundFalse = true; }
	bool hasFoundTrue(void) const { return foundTrue; }
	bool hasFoundFalse(void) const { return foundFalse; }
private:
	bool	foundTrue;
	bool	foundFalse;
};

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

	uint64_t getOffsetBits(void) const { return offset; }

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
