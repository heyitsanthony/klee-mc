#ifndef EXEMPTS_H
#define EXEMPTS_H

#include "guest.h"
extern "C" {
#include <valgrind/libvex_guest_amd64.h>
}
/* lol vex breaks things */
#ifdef True
#undef True
#undef False
#undef Bool
#endif
#include <stddef.h>
#include <vector>

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
		ret.push_back(Exemptent(offsetof(VexGuestAMD64State, guest_DFLAG), 8));
		ret.push_back(Exemptent(offsetof(VexGuestAMD64State, guest_FS_CONST), 8));
		break;
	case Arch::ARM:
		ret.push_back(Exemptent(380 /* TPIDRURO */, 4));
		break;
	default:
		assert (0 == 1 && "UNSUPPORTED ARCHITECTURE");
	}

	return ret;
}

#endif
