#include <sys/mman.h>

#include "symbols.h"
#include "guest.h"
#include "guestcpustate.h"

#include "klee/Internal/ADT/Crumbs.h"
#include "klee/Internal/ADT/KTestStream.h"
#include "static/Sugar.h"

#include "../klee-mc/ExeUC.h"

using namespace klee;

#define PAGE_SZ	0x1000UL

typedef std::map<std::string, std::vector<char> > ucbuf_map_ty;

static void loadUCBuffers(Guest* gs, KTestStream* kts_uc)
{
	std::set<void*>		uc_pages;
	ucbuf_map_ty		ucbufs;
	const UCTabEnt		*uctab;
	char			*lentab;
	const KTestObject	*kto;
	char			*cpu_state;
	unsigned		cpu_len;

	
	lentab = kts_uc->feedObjData(22060); // XXX need to be smarter
	uctab = (const UCTabEnt*)lentab;

	while ((kto = kts_uc->nextObject()) != NULL) {
		ucbufs[kto->name] = std::vector<char>(
			kto->bytes, kto->bytes+kto->numBytes);
	}

	foreach (it, ucbufs.begin(), ucbufs.end()) {
		std::string	uc_name(it->first);
		void		*base_ptr;
		unsigned	idx;

		std::cerr << uc_name << '\n';

		std::cerr << "BUF SIZE: " << it->second.size() << '\n';

		// uc_buf_n => uc_buf_n + 7 = n
		idx = atoi(uc_name.c_str() + 7);
		assert (idx < (22060 / sizeof(*uctab)) &&
			"UCBUF OUT OF BOUNDS");
		assert (uctab[idx].len == it->second.size());

		base_ptr = NULL;
		if (uctab[idx].real_ptr) base_ptr = uctab[idx].real_ptr;
		if (uctab[idx].sym_ptr) base_ptr = uctab[idx].sym_ptr;
		assert (base_ptr != NULL &&
			"No base pointer for ucbuf given!");

		std::cerr << "BASE_PTR: " << (void*)base_ptr << '\n';
		uc_pages.insert((void*)((uintptr_t)base_ptr & ~(PAGE_SZ-1)));
		uc_pages.insert((void*)(((uintptr_t)base_ptr+uctab[idx].len-1) & ~(PAGE_SZ-1)));

	}

	foreach (it, uc_pages.begin(), uc_pages.end()) {
		void	*new_page, *mapped_addr;
		
		new_page = *it;
		mapped_addr = mmap(
			new_page,
			PAGE_SZ,
			PROT_READ | PROT_WRITE, 
			MAP_PRIVATE | MAP_ANONYMOUS,
			-1,
			0);
		if (mapped_addr != new_page) {
			std::cerr << "FAILED TO MAP " << new_page << '\n';
			assert (0 == 1 && "OOPS");
		}
		std::cerr << "MAPPED: " << mapped_addr << '\n';
	}


	cpu_state = (char*)gs->getCPUState()->getStateData();
	cpu_len = gs->getCPUState()->getStateSize();
	foreach (it, ucbufs.begin(), ucbufs.end()) {
		std::string	uc_name(it->first);
		void		*base_ptr;
		unsigned	idx;

		idx = atoi(uc_name.c_str() + 7);
		base_ptr = NULL;
		if (uctab[idx].real_ptr) base_ptr = uctab[idx].real_ptr;
		if (uctab[idx].sym_ptr) base_ptr = uctab[idx].sym_ptr;

		for (unsigned i = 0; i < it->second.size(); i++)
			((char*)base_ptr)[i] = it->second[i];

		if (idx < cpu_len / 8) {
			assert (gs->getArch() == Arch::X86_64);
			memcpy(cpu_state + idx*8, &base_ptr, 8);
		}
	}

	std::cerr << "Copied in unconstrained buffers\n";
}

KTestStream* setupUCFunc(
	Guest		*gs,
	const char	*func,
	const char	*dirname,
	unsigned	test_num)
{
	KTestStream	*kts_uc, *kts_klee;
	const Symbol	*sym;
	char		fname_kts[256];
	char		*regfile_uc, *regfile_gs;

	/* patch up for UC */
	printf("Using func: %s\n", func);

	snprintf(
		fname_kts,
		256,
		"%s/test%06d.ucktest.gz", dirname, test_num);
	kts_uc = KTestStream::create(fname_kts);

	snprintf(
		fname_kts,
		256,
		"%s/test%06d.ktest.gz", dirname, test_num);
	kts_klee = KTestStream::create(fname_kts);

	assert (kts_uc && kts_klee);

	/* 1. setup environment */

	/* 1.a setup register values */
	Exempts	ex(ExeUC::getRegExempts(gs));
	regfile_uc = kts_uc->feedObjData(gs->getCPUState()->getStateSize());
	regfile_gs = (char*)gs->getCPUState()->getStateData();
	foreach (it, ex.begin(), ex.end()) {
		memcpy(regfile_uc+it->first, regfile_gs+it->first, it->second);
	}
	memcpy(regfile_gs, regfile_uc, gs->getCPUState()->getStateSize());
	delete regfile_uc;

	/* 1.b resteer execution to function */
	sym = gs->getSymbols()->findSym(func);
	guest_ptr	func_ptr(guest_ptr(sym->getBaseAddr()));
	std::cerr << "HEY: " << (void*)func_ptr.o << '\n';
	gs->getCPUState()->setPC(func_ptr);

	std::cerr << "WOO: " << (void*)gs->getCPUState()->getPC().o << '\n';

	/* return to 'deadbeef' when done executing */
	gs->getMem()->writeNative(
		gs->getCPUState()->getStackPtr(),
		0xdeadbeef);

	/* 2. scan through ktest, allocate buffers */
	loadUCBuffers(gs, kts_uc);

	/* done setting up buffers, drop UC ktest data */
	delete kts_uc;

	/* return stripped ktest file */
	return kts_klee;
}


