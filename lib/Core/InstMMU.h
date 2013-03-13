#ifndef INSTMMU_H
#define INSTMMU_H

#include "KleeMMU.h"

namespace klee
{
class Executor;
class KFunction;

class InstMMU : public KleeMMU
{
public:
	InstMMU(Executor& exe);
	virtual ~InstMMU(void) {}

	static KFunction	*f_store8, *f_store16, *f_store32,
				*f_store64, *f_store128;
	static KFunction	*f_load8, *f_load16, *f_load32,
				*f_load64, *f_load128;
	static KFunction	*f_cleanup;

private:
	void initModule(Executor& exe);
};

}

#endif
