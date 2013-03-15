#ifndef INSTMMU_H
#define INSTMMU_H

#include "KleeMMU.h"

namespace klee
{
class Executor;
class KFunction;
class SoftMMUHandlers;

class InstMMU : public KleeMMU
{
public:
	InstMMU(Executor& exe);
	virtual ~InstMMU(void);
private:
	SoftMMUHandlers	*mh;
};

}

#endif
