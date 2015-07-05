#include "../../lib/Core/StatsTracker.h"
#include "klee/Internal/ADT/Hash.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <llvm/Support/CommandLine.h>
#include <unistd.h>
#include <assert.h>
#include "ExecutorVex.h"
#include "KModuleVex.h"
#include "guest.h"
#include "genllvm.h"
#include "vexsb.h"
#include "vexxlate.h"
#include "vexfcache.h"

#include "Passes.h"
#include <sstream>
#include <stdio.h>

using namespace llvm;
using namespace klee;

#define isNameUgly(x)	(((x).find("sb_") == 0) || ((x).find("0x") == 0))

namespace
{
	cl::opt<bool> PrintNewRanges(
		"print-new-ranges",
		cl::desc("Print uncovered address ranges"),
		cl::init(false));

	cl::opt<bool> PrintNewTrace(
		"print-new-trace",
		cl::desc("Print uncovered address stack trace"),
		cl::init(false));

	cl::opt<bool> PrintLibraryName(
		"print-library-name",
		cl::desc("Print library name of uncovered function"),
		cl::init(true));

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

KModuleVex::KModuleVex(
	Executor* _exe,
	ModuleOptions& mod_opts,
	Guest* _gs)
: KModule(theGenLLVM->getModule(), mod_opts)
, exe(_exe)
, gs(_gs)
, ctrl_graph(gs)
, native_code_bytes(0)
, in_scan(false)
{
	xlate = std::make_shared<VexXlate>(gs->getArch());
	xlate_cache = std::make_unique<VexFCache>(xlate);
}

KModuleVex::~KModuleVex(void) {}

Function* KModuleVex::getFuncByAddrNoKMod(uint64_t guest_addr, bool& is_new)
{
	void		*host_addr;
	Function	*f;

	is_new = false;
	if (	guest_addr < 0x1000 ||
// XXX: this is needed to do kernel stuff
//		((guest_addr > 0x7fffffffffffULL) &&
//		((guest_addr & 0xfffffffffffff000) != 0xffffffffff600000)) ||
		guest_addr == 0xffffffff)
	{
		/* short circuit obviously bad addresses */
		return NULL;
	}

	/* XXX: This is wrong because it doesn't acknowledge write-backs */
	/* The right way to do it would involve grabbing from the state's MO */
	host_addr = gs->getMem()->getHostPtr(guest_ptr(guest_addr));

	/* cached => already seen it */
	/* XXX: this is broken if two states have different code mapped
	 * to the same address. Does that happen often? */
	f = xlate_cache->getCachedFunc(guest_ptr(guest_addr));
	if (f != NULL) {
		/* is_new = false */
		return f;
	}

	/* Need to load the function. First, make sure that addr is mapped. */
	is_new = true;

	GuestMem::Mapping	m;
	if (gs->getMem()->lookupMapping(guest_ptr(guest_addr), m) == false) {
		/* not in base mapping-- write during symex! */
		f = getPrivateFuncByAddr(guest_addr);
		return f;
	}

	f = loadFuncByBuffer(host_addr, guest_ptr(guest_addr));
	return f;
}

Function* KModuleVex::loadFuncByBuffer(void* host_addr, guest_ptr guest_addr)
{
	VexSB		*vsb;
	Function	*f;

	/* !cached => put in cache, alert kmodule, other bookkepping */
	f = xlate_cache->getFunc(host_addr, guest_addr);
	if (f == NULL) return NULL;

	/* need to know func -> vsb to compute func's guest address */
	vsb = xlate_cache->getCachedVSB(guest_addr);
	assert (vsb && "Dropped VSB too early?");
	func2vsb_table.insert(std::make_pair((uint64_t)f, vsb));

	native_code_bytes += vsb->getEndAddr() - vsb->getGuestAddr();

	if (!PrintNewRanges)
		return f;

	std::cerr << "[UNCOV] "
		<< (void*)vsb->getGuestAddr().o
		<< "-"
		<< (void*)vsb->getEndAddr().o << " : "
		<< gs->getName(vsb->getGuestAddr());

	if (PrintLibraryName) {
		if (ExecutionState *es = exe->getCurrentState()) {
			const MemoryObject	*mo;
			mo = es->addressSpace.resolveOneMO(guest_addr.o);
			if (mo == NULL) {
				std::cerr << " @ NoMemObj???";
			} else
				std::cerr << " @ " << mo->name;
		}
	}

	std::cerr << '\n';

	if (PrintNewTrace) {
		if (ExecutionState *es = exe->getCurrentState())
			exe->printStackTrace(*es, std::cerr);
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

Function* KModuleVex::getPrivateFuncByAddr(uint64_t guest_addr)
{
	uint8_t			buf[4096];
	const ExecutionState	*ese;
	ObjectPair		op;
	int			br;

	ese = exe->getCurrentState();
	if (ese == NULL)
		return NULL;

	memset(buf, 0xcc, sizeof(buf));
	br = ese->addressSpace.readConcreteSafe(buf, guest_addr, sizeof(buf));
	if (br == 0)
		return NULL;

	/* XXX: what if br comes up short?
	 * (e.g., code split between two pages)
	 * XXX: need intrinsic for addressspace concrete safe copy */

	/* is this a new library? if so, I'll need to be able to 
	 * load the symbols! */
	if (isNameUgly(gs->getName(guest_ptr(guest_addr)))) {
		/* ugly name? maybe a library was loaded */
		loadPrivateLibrary(guest_ptr(guest_addr));
	}

	return loadFuncByBuffer(buf, guest_ptr(guest_addr));
}

void KModuleVex::loadPrivateLibrary(guest_ptr addr)
{
	const ExecutionState	*ese;
	const MemoryObject	*cur_mo, *prev_mo;
	std::string		path;
	static std::set<std::string> seen_paths;

	ese = exe->getCurrentState();
	cur_mo = ese->addressSpace.resolveOneMO(addr.o);
	path = cur_mo->name;

	if (seen_paths.count(path))
		return;

	seen_paths.insert(path);

	/* mo path was not a path on the system */
	if (access(cur_mo->name.c_str(), R_OK) != 0)
		return;

	/* scan for base of mapping */
	while (1) {
		prev_mo = ese->addressSpace.resolveOneMO(cur_mo->address - 1);
		if (prev_mo == NULL)
			break;
		if (prev_mo->name != path)
			break;

		cur_mo = prev_mo;
	}

	/* tell guest about this new library */
	gs->addLibrarySyms(path.c_str(), guest_ptr(cur_mo->address));
}

/* decode error! kind of hacky */
Function* KModuleVex::handleDecodeError(uint64_t guest_addr)
{
	ExecutionState	*es;

	if (exe == NULL) return NULL;

	es = exe->getCurrentState();
	if (es == NULL) return NULL;

	/* how to recover from decode errors:
	 * 1. fork state indicating there was a decode error
	 * 2. find location of undecodeable instruction
	 * 3. create basic block up to undecodeable instruction,
	 *    replace undecodeable instruction with HOST_EXE intrinsic
	 *    that takes address. HOST_EXE returns next instruction pointer to
	 *    jump to.
	 * */ 

	TERMINATE_ERRORV(
		exe,
		*es,
		"Bad instruction decode",
		"decode.err",
		"Code Address: ", guest_addr);

	return NULL;
}

#define LIBRARY_BASE_GUESTADDR	((uint64_t)0x10000000)

Function* KModuleVex::getFuncByAddr(uint64_t guest_addr)
{
	Function	*f;
	bool		is_new;

	f = getFuncByAddrNoKMod(guest_addr, is_new);
	if (f == NULL) {
		if (is_new == false) {
			/* is_new = false => access error, handled normally */
			return NULL;
		}

		handleDecodeError(guest_addr);
		return NULL;
	}

	if (is_new) analyzeNewFunction(guest_addr, f);

	return f;
}

void KModuleVex::analyzeNewFunction(uint64_t guest_addr, Function* f)
{
	KFunction	*kf;

	/* do light analysis on new function */
	if (UseCtrlGraph) {
		static int dump_c = 0;
		ctrl_graph.addFunction(f, guest_ptr(guest_addr));
		dump_c = (dump_c + 1) % 512;
		if (dump_c == 0) {
			auto of = exe->getInterpreterHandler()->openOutputFile("statics.dot");
			if (of) ctrl_graph.dumpStatic(*of);
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

	if (const ExecutionState* ese = exe->getCurrentState()) {
		const MemoryObject	*mo;
		mo = ese->addressSpace.resolveOneMO(guest_addr);
		assert (mo != NULL);
		if (!mo->name.empty())
			setModName(kf, mo->name.c_str());
	}

	exe->getStatsTracker()->addKFunction(kf);
	bindKFuncConstants(exe, kf);
	bindModuleConstTable(exe);

	if (ScanExits && in_scan == false) {
		in_scan = true;
		scanFuncExits(guest_addr, f);
		in_scan = false;
	}
}

KFunction* KModuleVex::addFunction(Function* f)
{
	KFunction	*kf;
	bool		is_special;
	std::string	pretty_name;

	if (f->isDeclaration()) return NULL;

	is_special = isNameUgly(f->getName().str());
	if (is_special) {
		const VexSB	*vsb;
		if ((vsb = getVSB(f)) != NULL) {
			pretty_name = gs->getName(vsb->getGuestAddr());
			if (!isNameUgly(pretty_name) != 0)
				setPrettyName(f, pretty_name);
		}
	}

	kf = KModule::addFunction(f);
	if (kf) {
		kf->isSpecial = is_special;
		/* set pretty name again to add kf to mapping */
		setPrettyName(f, pretty_name);
	}

	return kf;
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

void KModuleVex::prepare(InterpreterHandler *ihandler)
{
	KModule::prepare(ihandler);
	addFunctionPass(new RemovePCPass());
	addFunctionPass(new OutcallMCPass());
}
