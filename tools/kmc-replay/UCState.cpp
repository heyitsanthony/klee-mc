#include "klee/Internal/ADT/Crumbs.h"
#include "klee/Internal/ADT/KTestStream.h"
#include "static/Sugar.h"
#include <stdio.h>
#include <sys/mman.h>
#include <assert.h>
#include "guestmem.h"

#include "../klee-mc/ExeUC.h"
#include "UCState.h"
#include "UCBuf.h"
#include "symbols.h"

using namespace klee;

#define PAGE_SZ	0x1000UL

typedef std::map<std::string, std::vector<char> > ucdata_map_ty;


template <typename UCTabEnt>
void UCState::loadUCBuffers(Guest* gs, KTestStream* kts_uc)
{
	const UCTabEnt		*uctab;
	char			*lentab;
	const KTestObject	*kto;
	char			*cpu_state;
	unsigned		cpu_len;
	ucdata_map_ty		ucdata;

	lentab = kts_uc->feedObjData(); // XXX need to be smarter
	uctab = (const UCTabEnt*)lentab;

	while ((kto = kts_uc->nextObject()) != NULL) {
		ucdata[kto->name] = std::vector<char>(
			kto->bytes, kto->bytes+kto->numBytes);
	}

	/* create UCBufs from ucdata */
	foreach (it, ucdata.begin(), ucdata.end()) {
		std::string	uc_name(it->first);
		uint64_t	base_ptr;
		unsigned	idx;

		std::cerr << uc_name << '\n';

		// uc_buf_n => uc_buf_n + 7 = n
		idx = UCBuf::getPtIdx(uc_name);
		assert (idx < (22060 / sizeof(*uctab)) &&
			"UCBUF OUT OF BOUNDS");

		assert (uctab[idx].len == it->second.size());

		base_ptr = NULL;
		if (uctab[idx].real_ptr) base_ptr = uctab[idx].real_ptr;
		if (uctab[idx].sym_ptr) base_ptr = uctab[idx].sym_ptr;
		assert (base_ptr &&
			"No base pointer for ucbuf given!");

		ucbufs[uc_name] = new UCBuf(
			uc_name,
			guest_ptr(base_ptr),
			uctab[idx].len,
			it->second);
	}

	/* build page list */
	foreach (it, ucbufs.begin(), ucbufs.end()) {
		UCBuf	*ucb = it->second;

		uc_pages.insert(ucb->getBase().o & ~(PAGE_SZ-1));
		uc_pages.insert(
			(ucb->getBase().o+ucb->getUsedLength()-1) & ~(PAGE_SZ-1));
	}


	/* map in ucbufs */
	foreach (it, uc_pages.begin(), uc_pages.end()) {
		guest_ptr	new_page, mapped_addr;
		int		err;

		new_page = guest_ptr(*it);
		err = gs->getMem()->mmap(
			mapped_addr,
			new_page,
			PAGE_SZ,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
			-1,
			0);
		if (mapped_addr != new_page) {
			std::cerr << "FAILED TO MAP " << new_page << '\n';
			assert (0 == 1 && "OOPS");
		}
	}

	/* copy in ucbufs */
	cpu_state = (char*)gs->getCPUState()->getStateData();
	cpu_len = gs->getCPUState()->getStateSize();
	foreach (it, ucbufs.begin(), ucbufs.end()) {
		UCBuf		*ucb = it->second;
		uint64_t	base_ptr;
		unsigned	idx;

		base_ptr = ucb->getBase().o;
		idx = ucb->getIdx();

		/* copy pointer to root area */
		if (gs->getMem()->is32Bit()) {
			if (idx < cpu_len / 4) {
				memcpy(cpu_state + idx*4, &base_ptr, 4);
			}
		} else if (idx < cpu_len / 8) {
			memcpy(cpu_state + idx*8, &base_ptr, 8);
		}

		/* buffers from klee are actually 8-aligned,
		 * so adjust copy-out to start at 8-byte boundary */
		gs->getMem()->memcpy(
			ucb->getAlignedBase(),
			ucb->getData(),
			ucb->getDataLength());

	}

	std::cerr << "Copied in unconstrained buffers\n";
}

UCState* UCState::init(
	Guest* gs,
	const char	*funcname,
	const char	*dirname,
	unsigned	test_num)
{
	/* patch up for UC */
	UCState	*ucs;

	printf("Using func: %s\n", funcname);

	ucs = new UCState(gs, funcname, dirname, test_num);
	if (ucs->ok) {
		return ucs;
	}

	delete ucs;
	return NULL;
}

/* return stripped ktest file */
KTestStream* UCState::allocKTest(void) const
{
	char	fname_kts[256];
	snprintf(
		fname_kts,
		256,
		"%s/test%06d.ktest.gz", dirname, test_num);
	return KTestStream::create(fname_kts);
}

void UCState::setupRegValues(KTestStream* kts_uc)
{
	Exempts		ex(ExeUC::getRegExempts(gs));
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
	const char	*in_dirname,
	unsigned	in_test_num)
: gs(in_gs)
, funcname(in_func)
, dirname(in_dirname)
, test_num(in_test_num)
, ok(false)
{
	KTestStream	*kts_uc;
	const Symbol	*sym;
	char		fname_kts[256];

	/* 1. setup environment */
	snprintf(
		fname_kts,
		256,
		"%s/test%06d.ucktest.gz", dirname, test_num);
	kts_uc = KTestStream::create(fname_kts);

	assert (kts_uc);

	/* 1.a setup register values */
	setupRegValues(kts_uc);

	/* 1.b resteer execution to function */
	sym = gs->getSymbols()->findSym(funcname);
	if (sym == NULL) {
		std::cerr << "UC Function '" << funcname << "' not found. ULP\n";
		delete kts_uc;
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
		((uint32_t*)gs->getCPUState()->getStateData())[14] = 0xdeadbeef;
	} else {
		gs->getMem()->writeNative(
			gs->getCPUState()->getStackPtr(),
			0xdeadbeef);
	}

	/* 2. scan through ktest, allocate buffers */
	if (gs->getMem()->is32Bit())
		loadUCBuffers<UCTabEnt32>(gs, kts_uc);
	else
		loadUCBuffers<UCTabEnt64>(gs, kts_uc);

	/* done setting up buffers, drop UC ktest data */
	delete kts_uc;

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
}

UCState::~UCState(void)
{
	foreach (it, ucbufs.begin(), ucbufs.end())
		delete it->second;
}
