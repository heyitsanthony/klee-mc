#ifndef BFSBUILDER_H
#define BFSBUILDER_H

#include "klee/ExprBuilder.h"
#include "BitfieldSimplifier.h"

namespace klee
{

class BitfieldSimplifierBuilder : public ExprBuilder, BitfieldSimplifier
{
public:
	BitfieldSimplifierBuilder(ExprBuilder* _eb) : eb(_eb) {}
	virtual ~BitfieldSimplifierBuilder() { delete eb; } 
	void printName(std::ostream& os) const {
		os << "BitfieldSimplifier(";
		eb->printName(os);
		os << ")";
	}
EXPR_BUILDER_DECL_ALL
protected:
	BitfieldSimplifierBuilder(void) {}
private:
	ExprBuilder	*eb;
};

}

#endif
