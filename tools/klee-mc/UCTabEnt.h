#ifndef UC_TABENT_H
#define UC_TABENT_H

#include <stdint.h>
#include <vector>
#include "guest.h"

namespace klee
{

typedef std::pair<unsigned /* off */, unsigned /* len */> Exemptent;
typedef std::vector<Exemptent> Exempts;

/* inlined so that kmc-replay will work */
static inline Exempts getRegExempts(const Guest* gs)
{
	Exempts	ret;

	ret.push_back(
		Exemptent(
			gs->getCPUState()->getStackRegOff(),
			(gs->getMem()->is32Bit()) ? 4 : 8));

	switch (gs->getArch()) {
	case Arch::X86_64:
		ret.push_back(Exemptent(160 /* guest_DFLAG */, 8));
		ret.push_back(Exemptent(192 /* guest_FS_ZERO */, 8));
		break;
	case Arch::ARM:
		ret.push_back(Exemptent(380 /* TPIDRURO */, 4));
		break;
	default:
		assert (0 == 1 && "UNSUPPORTED ARCHITECTURE");
	}


	return ret;
}



#pragma pack(1)
struct UCTabEnt64
{
	uint32_t	len;
	uint64_t	sym_ptr;
	uint64_t	real_ptr;
	uint64_t	init_align;
};

struct UCTabEnt32
{
	uint32_t	len;
	uint32_t	sym_ptr;
	uint32_t	real_ptr;
	uint32_t	init_align;
};

#pragma pack()
}
#endif
