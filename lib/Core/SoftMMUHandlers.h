#ifndef SOFTMMUHANDLERS_H
#define SOFTMMUHANDLERS_H

#include <string>

namespace klee
{
class KFunction;
class KModule;
class Executor;

/* XXX: might screw up for 1-bit ops, but seems to work fine now */
#define BITSWITCH(x)		\
	switch (bits) {		\
	case 1: case 8: return x[0];	\
	case 16: return x[1];	\
	case 32: return x[2];	\
	case 64: return x[3];	\
	case 128: return x[4]; } return NULL;


class SoftMMUHandlers
{
public:
	SoftMMUHandlers(Executor& exe, const std::string& suffix);
	virtual ~SoftMMUHandlers(void) {}

	KFunction* getStore(unsigned bits) const { BITSWITCH(f_store); }

	KFunction* getLoad(unsigned bits) const { BITSWITCH(f_load); }

	KFunction *getCleanup(void) const { return f_cleanup; }
	KFunction *getInit(void) const { return f_init; }
	KFunction *getSignal(void) const { return f_signal; }
private:
	void loadBySuffix(Executor& exe, const std::string& suffix);
	void loadByFile(Executor& exe, const std::string& fname);

	/* f[i] => 2^(i+3) width in bits */
	KFunction *f_store[5], *f_load[5];
	KFunction *f_cleanup, *f_init, *f_signal;

	static bool isLoaded;
};
}

#endif
