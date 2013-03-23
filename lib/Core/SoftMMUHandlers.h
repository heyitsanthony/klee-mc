#ifndef SOFTMMUHANDLERS_H
#define SOFTMMUHANDLERS_H

#include <string>

namespace klee
{
class KFunction;
class KModule;
class Executor;

class SoftMMUHandlers
{
public:
	SoftMMUHandlers(Executor& exe, const std::string& suffix);
	virtual ~SoftMMUHandlers(void) {}

	KFunction* getStore(unsigned bits) const {
		switch (bits) {
		case 1:
		case 8: return f_store[0];
		case 16: return f_store[1];
		case 32: return f_store[2];
		case 64: return f_store[3];
		case 128: return f_store[4];
		}
		return NULL; }

	KFunction* getLoad(unsigned bits) const {
		switch (bits) {
		/* XXX: this might screw up for 1-bit ops,
		 * but it seems to work fine now */
		case 1:
		case 8: return f_load[0];
		case 16: return f_load[1];
		case 32: return f_load[2];
		case 64: return f_load[3];
		case 128: return f_load[4];
		}
		return NULL; }

	KFunction *getCleanup(void) const { return f_cleanup; }
	KFunction *getInit(void) const { return f_init; }


private:
	/* f[i] => 2^(i+3) width in bits */
	KFunction *f_store[5];
	KFunction *f_load[5];
	KFunction *f_cleanup, *f_init;

	static bool isLoaded;
};
}

#endif
