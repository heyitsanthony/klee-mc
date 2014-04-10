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
EXPR_BUILDER_DECL_ALL
protected:
	BitfieldSimplifierBuilder(void) {}
private:
	ExprBuilder	*eb;
};

}

#endif
