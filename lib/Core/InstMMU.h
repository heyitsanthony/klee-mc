#ifndef INSTMMU_H
#define INSTMMU_H

#include "KleeMMU.h"

namespace klee
{
class Executor;
class SoftMMUHandlers;
class InstMMU : public KleeMMU
{
public:
	InstMMU(Executor& exe);
private:
	std::unique_ptr<SoftMMUHandlers> mh;
};
}

#endif
