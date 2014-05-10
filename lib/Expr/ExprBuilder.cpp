#include <iostream>
#include "klee/ExprBuilder.h"
#include "OptBuilder.h"
#include "ExtraOptBuilder.h"
#include "RuleBuilder.h"
#include "CanonBuilder.h"
#include "ChainedBuilder.h"
#include "BitfieldSimplifierBuilder.h"

using namespace klee;

ExprBuilder* ExprBuilder::create(BuilderKind bk)
{
	ExprBuilder *Builder = createDefaultExprBuilder();
	switch (bk) {
	case DefaultBuilder:
		return Builder;
	case ConstantFoldingBuilder:
		Builder = createConstantFoldingExprBuilder(Builder);
		break;
	case SimplifyingBuilder:
		Builder = createConstantFoldingExprBuilder(Builder);
		Builder = createSimplifyingExprBuilder(Builder);
		break;
	case HandOptBuilder:
		delete Builder;
		Builder = new OptBuilder();
		break;
	case ExtraOptsBuilder:
		delete Builder;
		Builder = new ExtraOptBuilder();
		break;
	case BitSimplifier:
		Builder = new BitfieldSimplifierBuilder(Builder);
		break;

	case RuleBuilder:
//		return RuleBuilder::create(new CanonBuilder(Builder));
		return RuleBuilder::create(Builder);
	default:
		std::cerr << "Unknown BuilderKind.\n";
		assert (0 == 1);
		break;
	}

//	return new CanonBuilder(Builder);
	return Builder;
}

void ChainedEB::printName(std::ostream& os) const
{
	os << "ChainedBuilder {\n";
	os << "Base: ";
	Base->printName(os);
	os << "Builder; ";
	Builder->printName(os);
	os << "}\n";
}
