#include "klee/Internal/ADT/Crumbs.h"
#include "klee/Internal/ADT/KTestStream.h"
#include "static/Sugar.h"
#include <stdio.h>
#include <sys/mman.h>
#include <assert.h>
#include "guestmem.h"
#include "guestcpustate.h"
#include "../klee-mc/Exempts.h"
#include "../../runtime/mmu/uc.h"
#include "UCState.h"
#include "UCBuf.h"
#include "symbols.h"
using namespace klee;

typedef std::vector<std::vector<char> > ucbs_raw_ty;
typedef std::vector<struct uce_backing*> uc_backings_ty;
typedef std::vector<struct uc_ent > uc_ents_ty;

void UCState::loadUCBuffers(Guest* gs, KTestStream* kts)
{
	const KTestObject	*kto;
	uc_ents_ty		uc_ents;
	ucbs_raw_ty		uc_b_raw;
	uc_backings_ty		uc_backings;

	/* collect UC data */
	while ((kto = kts->nextObject()) != NULL) {
		int uce_prefix, ucb_prefix;

		uce_prefix = strncmp("uce_", kto->name, 4);
		ucb_prefix = strncmp("ucb_", kto->name, 4);

		if (!uce_prefix && kto->numBytes == sizeof(struct uc_ent)) {
			struct uc_ent	*uce;
			uce = static_cast<struct uc_ent*>((void*)kto->bytes);
			uc_ents.push_back(*uce);
		}

		if (!ucb_prefix) {
			uc_b_raw.push_back(std::vector<char>(
				kto->bytes, kto->bytes + kto->numBytes));
		}
	}

	/* load largest fitting backing for given index */
	uc_backings.resize(uc_ents.size());
	foreach (it, uc_b_raw.begin(), uc_b_raw.end()) {
		struct uce_backing	*ucb;
		ucb = static_cast<struct uce_backing*>((void*)(*it).data());
		uc_backings[ucb->ucb_uce_n - 1] = ucb;
	}

	ucbufs.resize(uc_ents.size());
	foreach (it, uc_ents.begin(), uc_ents.end()) {
		struct uc_ent		uce(*it);
		unsigned		idx;

		idx = uce.uce_n - 1;
		if (uce.uce_n == 0 || uc_backings[idx] == NULL) {
			std::cerr << "No backing on idx=" << uce.uce_n << '\n';
			continue;
		}

		std::cerr << "[UC] RADIUS: " << uce.uce_radius << '\n';
		std::cerr << "[UC] PIVOT: " << uce.access.a_pivot << '\n';

		std::vector<char>	init_dat(
			uc_backings[idx]->ucb_dat,
			uc_backings[idx]->ucb_dat + 1 + uce.uce_radius*2);

		ucbufs[idx] = new UCBuf(
			gs,
			(uint64_t)uce.access.a_pivot, uce.uce_radius, init_dat);
	}

	/* link back into register state */
#if 0
	foreach (it, uc_ents.begin(), uc_ents.end()) {
		struct uc_ent	uce(*it);
		unsigned	idx, cpu_len, best_idx;
		uint64_t	min_off;
		uint64_t	*cpu;

		idx = uce.uce_n - 1;
		if (uce.uce_n == 0 || ucbufs[idx] == NULL)
			continue;

		cpu = (uint64_t*)gs->getCPUState()->getStateData();
		cpu_len = gs->getCPUState()->getStateSize() / sizeof(uint64_t);
		min_off = ~((uint64_t)0);
		best_idx = 0;
		for (unsigned i = 0; i < cpu_len; i++) {
			int64_t	cur_off;

			cur_off = cpu[i] - (uint64_t)uce.access.a_pivot;
			if (cur_off < 0) cur_off = -cur_off;
			if ((uint64_t)cur_off < min_off) {
				min_off = cur_off;
				best_idx = i;
				std::cerr << "[UC] minoff=" << min_off << '\n';
			}
		}

		/* is this a smart fixup method? */
		min_off = (cpu[best_idx] - (uint64_t)uce.access.a_pivot);
		std::cerr << "[UC] Fixing up idx=" << best_idx << ". Off=" <<
			(int64_t)min_off << '\n';
		cpu[best_idx] = ucbufs[idx]->getPivot() + min_off;
	}
#endif
}

UCState* UCState::init(
	Guest* gs,
	const char	*funcname,
	KTestStream	*kts)
{
	/* patch up for UC */
	UCState	*ucs;

	printf("Using func: %s\n", funcname);

	ucs = new UCState(gs, funcname, kts);
	if (ucs->ok) {
		return ucs;
	}

	delete ucs;
	return NULL;
}

void UCState::setupRegValues(KTestStream* kts_uc)
{
	Exempts		ex(getRegExempts(gs));
	char		*regfile_uc, *regfile_gs;

	regfile_uc = kts_uc->feedObjData(gs->getCPUState()->getStateSize());
	regfile_gs = (char*)gs->getCPUState()->getStateData();
	foreach (it, ex.begin(), ex.end()) {
		memcpy(regfile_uc+it->first, regfile_gs+it->first, it->second);
	}

	memcpy(regfile_gs, regfile_uc, gs->getCPUState()->getStateSize());
	delete [] regfile_uc;
}

UCState::UCState(
	Guest		*in_gs,
	const char	*in_func,
	KTestStream	*kts)
: gs(in_gs)
, funcname(in_func)
, ok(false)
{
	const Symbol	*sym;

	/* 1.a setup register values */
	setupRegValues(kts);

	/* 1.b resteer execution to function */
	sym = gs->getSymbols()->findSym(funcname);
	if (sym == NULL) {
		std::cerr << "UC Function '" << funcname << "' not found. ULP\n";
		return;
	}

	guest_ptr	func_ptr(guest_ptr(sym->getBaseAddr()));

	std::cerr
		<< "UC Function: "
		<< funcname << '@' << (void*)func_ptr.o << '\n';

	gs->getCPUState()->setPC(func_ptr);

	/* return to 'deadbeef' when done executing */
	if (gs->getArch() == Arch::ARM) {
		/* this is the wrong thing to do; I know */
		((uint32_t*)gs->getCPUState()->getStateData())[16] = 0xdeadbeef;
	} else {
		gs->getMem()->writeNative(
			gs->getCPUState()->getStackPtr(),
			0xdeadbeef);
	}

	/* 2. scan through ktest, allocate buffers */
	loadUCBuffers(gs, kts);

	ok = true;
}

void UCState::save(const char* fname) const
{
	FILE		*f;
	uint64_t	ret = 0;

	f = fopen(fname, "w");
	assert (f != NULL);

	fprintf(f, "<ucstate>\n");

	std::cerr << "SAVING!!!!!\n";

	assert (0 == 1 && "STUB");

#if 0
	/* write out buffers */
	foreach (it, ucbufs.begin(), ucbufs.end()) {
		UCBuf		*ucb = it->second;
		guest_ptr	aligned_base;

		aligned_base = ucb->getAlignedBase();
		fprintf(f, "<ucbuf name=\"%s\" addr=\"%p\" base=\"%p\">\n",
			ucb->getName().c_str(),
			(void*)ucb->getBase().o,
			(void*)aligned_base.o);

		for (unsigned k = 0; k < ucb->getUsedLength(); k++) {
			uint8_t	c;
			c = gs->getMem()->read<uint8_t>(aligned_base + k);
			fprintf(f, "%02x ", (unsigned)c);
		}

		fprintf(f, "\n</ucbuf>\n");
	}

	/* save return value */
	memcpy(	&ret,
		((const char*)gs->getCPUState()->getStateData()) +
		gs->getCPUState()->getRetOff(),
		gs->getMem()->is32Bit() ? 4 : 8);
	fprintf(f, "<ret>%p</ret>\n", (void*)ret);

	fprintf(f, "</ucstate>");
	fclose(f);
#endif
}

UCState::~UCState(void)
{
	foreach (it, ucbufs.begin(), ucbufs.end())
		delete (*it);
}
