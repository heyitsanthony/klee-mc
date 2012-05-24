#include "../../lib/Core/StatsTracker.h"
#include "klee/Internal/ADT/Hash.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <llvm/Support/CommandLine.h>
#include <assert.h>
#include "ExecutorVex.h"
#include "KModuleVex.h"
#include "guest.h"
#include "genllvm.h"
#include "vexsb.h"
#include "vexxlate.h"
#include "vexfcache.h"

#include <stdio.h>

using namespace llvm;
using namespace klee;

namespace
{
	cl::opt<bool> PrintNewRanges(
		"print-new-ranges",
		cl::desc("Print uncovered address ranges"),
		cl::init(false));

	cl::opt<bool> CountLibraries(
		"count-lib-cov",
		cl::desc("Count library coverage"),
		cl::init(true));

	cl::opt<bool> UseCtrlGraph(
		"ctrl-graph",
		cl::desc("Compute control graph."),
		cl::init(false));

	cl::opt<bool> ScanExits(
		"scan-exits",
		cl::desc("Statically scan new blocks."),
		cl::init(false));
}

KModuleVex::KModuleVex(Executor* _exe, Guest* _gs)
: KModule(theGenLLVM->getModule())
, exe(_exe)
, gs(_gs)
, ctrl_graph(gs)
, native_code_bytes(0)
, in_scan(false)
{
	xlate = new VexXlate(gs->getArch());
	xlate_cache = new VexFCache(xlate);
}

KModuleVex::~KModuleVex(void)
{
	delete xlate_cache;
	delete xlate;
}

Function* KModuleVex::getFuncByAddrNoKMod(uint64_t guest_addr, bool& is_new)
{
	void		*host_addr;
	Function	*f;
	VexSB		*vsb;

	if (	guest_addr < 0x1000 ||
		((guest_addr > 0x7fffffffffffULL) &&
		((guest_addr & 0xfffffffffffff000) != 0xffffffffff600000)) ||
		guest_addr == 0xffffffff)
	{
		/* short circuit obviously bad addresses */
		return NULL;
	}

	/* XXX: This is wrong because it doesn't acknowledge write-backs */
	/* The right way to do it would involve grabbing from the state's MO */
	host_addr = gs->getMem()->getHostPtr(guest_ptr(guest_addr));

	/* cached => already seen it */
	f = xlate_cache->getCachedFunc(guest_ptr(guest_addr));
	if (f != NULL) {
		is_new = false;
		return f;
	}

	/* Need to load the function. First, make sure that addr is mapped. */
	/* XXX: this is broken for code that is allocated *after* guest is
	 * loaded and snapshot */
	GuestMem::Mapping	m;
	if (gs->getMem()->lookupMapping(guest_ptr(guest_addr), m) == false) {
		return NULL;
	}

	/* !cached => put in cache, alert kmodule, other bookkepping */
	f = xlate_cache->getFunc(host_addr, guest_ptr(guest_addr));
	if (f == NULL) return NULL;

	/* need to know func -> vsb to compute func's guest address */
	vsb = xlate_cache->getCachedVSB(guest_ptr(guest_addr));
	assert (vsb && "Dropped VSB too early?");
	func2vsb_table.insert(std::make_pair((uint64_t)f, vsb));

	is_new = true;
	native_code_bytes += vsb->getEndAddr() - vsb->getGuestAddr();

	if (PrintNewRanges) {
		std::cerr << "[UNCOV] "
			<< (void*)vsb->getGuestAddr().o
			<< "-"
			<< (void*)vsb->getEndAddr().o << " : "
			<< gs->getName(vsb->getGuestAddr())
			<< '\n';
	}

	return f;
}

const VexSB* KModuleVex::getVSB(Function* f) const
{
	func2vsb_map::const_iterator	it;

	it = func2vsb_table.find((uint64_t)f);
	if (it == func2vsb_table.end())
		return NULL;

	return it->second;
}

#define LIBRARY_BASE_GUESTADDR	((uint64_t)0x10000000)

Function* KModuleVex::getFuncByAddr(uint64_t guest_addr)
{
	KFunction	*kf;
	Function	*f;
	bool		is_new;

	f = getFuncByAddrNoKMod(guest_addr, is_new);
	if (f == NULL) return NULL;
	if (!is_new) return f;

	/* do light analysis */
	if (UseCtrlGraph) {
		ctrl_graph.addFunction(f, guest_ptr(guest_addr));
		std::ostream* of;
		of = exe->getInterpreterHandler()->openOutputFile("statics.dot");
		if (of) {
			ctrl_graph.dumpStatic(*of);
			delete of;
		}
	}

	/* insert it into the kmodule */
	if (CountLibraries == false) {
		/* is library address? */
		if (guest_addr > LIBRARY_BASE_GUESTADDR) {
			kf = addUntrackedFunction(f);
		} else {
			kf = addFunction(f);
		}
	} else
		kf = addFunction(f);

	exe->getStatsTracker()->addKFunction(kf);
	bindKFuncConstants(exe, kf);
	bindModuleConstTable(exe);

	if (ScanExits && in_scan == false) {
		in_scan = true;
		scanFuncExits(guest_addr, f);
		in_scan = false;
	}

	return f;
}


//struct AuxCodeEnt {
//	uint64_t	code_base_ptr;
//	uint32_t	code_len;
//};

#define CODEGRAPH_DIR	"codegraphs"

void KModuleVex::writeCodeGraph(GenericGraph<guest_ptr>& g)
{
	FILE						*f;
	char						path[256];
	std::string					hash_str;
	std::vector<unsigned char>			outdat;
	std::vector<std::pair<guest_ptr, unsigned> >	auxdat;

	foreach (it, g.nodes.begin(), g.nodes.end()) {
		guest_ptr	p = (*it)->value;
		const VexSB	*vsb;
		llvm::Function	*func;
		guest_ptr	base;
		uint32_t	code_len;
		char		*code_buf;

		func = getFuncByAddr(p.o);
		if (func == NULL)
			continue;

		vsb = getVSB(func);
		base = vsb->getGuestAddr();
		code_len = vsb->getSize();

		code_buf = new char[code_len];
		gs->getMem()->memcpy(code_buf, base, code_len);

		outdat.insert(outdat.end(), code_buf, code_buf + code_len);
		auxdat.push_back(std::make_pair(base, code_len));

		delete [] code_buf;
	}

	hash_str = Hash::SHA(&outdat.front(), outdat.size());
	sprintf(path, CODEGRAPH_DIR"/%d/%s",
		g.getNumNodes(), hash_str.c_str());

	f = fopen(path, "w");
	if (f == NULL) {
		sprintf(path, CODEGRAPH_DIR"/%d", g.getNumNodes());
		if (mkdir(path, 0700) != 0)
			return;

		sprintf(path, CODEGRAPH_DIR"/%d/%s",
			g.getNumNodes(), hash_str.c_str());
		f = fopen(path, "w");
		if (f == NULL)
			return;

	}

	fwrite(&outdat.front(), outdat.size(), 1, f);
	fclose(f);

	sprintf(path, CODEGRAPH_DIR"/%d/%s.aux",
		g.getNumNodes(), hash_str.c_str());
	f = fopen(path, "w");
	fwrite(&auxdat.front(), auxdat.size()*sizeof(auxdat[0]), 1, f);
	fclose(f);
}

typedef std::pair<uint64_t, Function*> faddr_ty;

void KModuleVex::scanFuncExits(uint64_t guest_addr, Function* f)
{
	std::stack<faddr_ty>		f_stack;
	std::set<guest_ptr>		scanned_addrs;
	GenericGraph<guest_ptr>		g;

	assert (in_scan);

	f_stack.push(std::make_pair(guest_addr, f));
	g.addNode(guest_ptr(guest_addr));

	while (f_stack.empty() == false) {
		std::list<guest_ptr>	l;
		faddr_ty		fa;
		Function		*cur_f;

		fa = f_stack.top();
		f_stack.pop();

		cur_f = fa.second;
		if (cur_f == NULL)
			continue;

		l = DynGraph::getStaticReturnAddresses(cur_f);
		foreach (it, l.begin(), l.end()) {
			Function	*next_f;

			g.addEdge(guest_ptr(fa.first), *it);

			if (scanned_addrs.count(*it))
				continue;

			scanned_addrs.insert(*it);
			next_f = getFuncByAddr((*it).o);
			f_stack.push(std::make_pair(*it, next_f));
		}
	}

	writeCodeGraph(g);
}

