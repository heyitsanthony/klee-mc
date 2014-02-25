/* XXX: this needs better integration with ktest shadowing. don't know how */
/* going to need several modes:
 * 	1. pure concrete
 * 	2. concrete but use KTestStateSolver to seed values
 * 	3. ... */
#include <llvm/Support/CommandLine.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdio.h>
#include "../../lib/Core/SpecialFunctionHandler.h"
#include "klee/ExecutionState.h"
#include "ExecutorVex.h"
#include "ExeStateVex.h"
#include <valgrind/libvex_guest_amd64.h>

namespace
{
	llvm::cl::opt<std::string> OSSFxDir("ossfx-dir");
	llvm::cl::opt<bool> OSSFxExitOnDrain("ossfx-exit-on-drain");
}

using namespace klee;

static unsigned dispatched_ossfx_c = 0;
static bool	done_ossfx = false;

static void apply_ossfx_diff(
	ExecutionState& es,
	unsigned seq_nr,
	const char* dname)
{
	FILE		*f;
	char		path[256];
	char		line[80];
	uint64_t	basep;
	bool		mismatched = false;

	sscanf(dname, "%lx.cmp", &basep);

	sprintf(path, "%s/%d/%s", OSSFxDir.c_str(), seq_nr, dname);
	f = fopen(path, "rb");

	/* this is probably super slow but whatever */
	while (fgets(line, sizeof(line), f) != NULL) {
		uint64_t		off, os_off;
		unsigned int		old_v, new_v;
		const MemoryObject	*mo;
		ObjectState		*wos;
		uint64_t		addr;

		/* cmp format: offset octal_old octal_new */
		sscanf(line, "%ld %o %o", &off, &old_v, &new_v);
		/* offsets from 'cmp' start at 1 instead of 0; fixup */
		off--;

		addr = off + basep;

		mo = es.addressSpace.resolveOneMO(addr);
		wos = es.addressSpace.findWriteableObject(mo);

		assert (addr >= mo->address && "oob addr?");
		os_off = addr - mo->address;

		if (wos->isByteConcrete(os_off)) {
			unsigned int cur_v = wos->read8c(os_off);
			if (cur_v != old_v) {
				std::cerr << "MISMATCH?? Addr="
					<< (void*)addr << '\n'
					<< "Line: " << line 
					<< "Expected: " << old_v
					<< ". Got: " << cur_v << '\n';
				mismatched = true;
			}
		}

		es.write8(wos, os_off, new_v);
	}

	fclose(f);

	if (mismatched) {
		assert (0 == 1 && "STUB HOW TO HANDLE MISMATCH");
	}
}

static void apply_ossfx_mmap(
	Executor* exe,
	ExecutionState& es,
	unsigned seq_nr,
	const char* dname)
{
	FILE		*f;
	char		path[256];
	struct stat	s;
	uint64_t	basep;
	unsigned	i;
	uint8_t		*dat;

	sscanf(dname, "%lx.new", &basep);

	sprintf(path, "%s/%d/%s", OSSFxDir.c_str(), seq_nr, dname);
	f = fopen(path, "rb");
	stat(path, &s);

	for (i = 0; i < s.st_size; i += 4096)
		es.allocateAt(basep+i*4096, 4096, NULL);

	dat = new uint8_t[s.st_size];
	fread(dat, s.st_size, 1, f);
	fclose(f);

	for (i = 0; i < s.st_size; i++)
		if (dat[i] != '\0')
			break;

	if (i == s.st_size) {
		/* all zeros */
		delete [] dat;
		return;
	}

	/* copy data in */
	es.addressSpace.copyOutBuf(basep, (const char*)dat, s.st_size);
	delete [] dat;
}

static bool apply_ossfx_mem(
	Executor* exe,
	ExecutionState& es,
	unsigned seq_nr)
{
	char		path[256];
	DIR		*d;
	struct dirent	*de;

	/* load register files for pre and post snapshots */
	sprintf(path, "%s/%d/", OSSFxDir.c_str(), seq_nr);
	d = opendir(path);
	while ((de = readdir(d)) != NULL) {
		unsigned	len = strlen(de->d_name);
		const char	*ext = &de->d_name[len-4];

		if (len < 4) continue;

		if (strcmp(ext, ".new") == 0) {
			apply_ossfx_mmap(exe, es, seq_nr, de->d_name);
		} else if (strcmp(ext, ".cmp") == 0) {
			apply_ossfx_diff(es, seq_nr, de->d_name);
		}
	}

	closedir(d);

	return true;
}

static bool apply_ossfx_regs(
	Executor* exe,
	ExecutionState& es,
	unsigned seq_nr)
{
	char		path[512];
	ExeStateVex	&esv(es2esv(es));
	uint8_t		*regs_pre, *regs_post;
	struct stat	s;
	ObjectState	*regs;
	FILE		*f;
	Guest		*gs(esv.getBaseGuest());
	VexGuestAMD64State	*v1, *v2;

	/* load register files for pre and post snapshots */
	sprintf(path, "%s/%d/regs.pre", OSSFxDir.c_str(), seq_nr);
	if (stat(path, &s) == -1) return false;

	regs_pre = new uint8_t[s.st_size];
	regs_post = new uint8_t[s.st_size];

	f = fopen(path, "rb");
	fread(regs_pre, s.st_size, s.st_size, f);
	fclose(f);

	sprintf(path, "%s/%d/regs.post", OSSFxDir.c_str(), seq_nr);
	f = fopen(path, "rb");
	fread(regs_post, s.st_size, s.st_size, f);
	fclose(f);

	regs = esv.getRegObj();

	assert (gs->getArch() == Arch::X86_64);

	/* hurrr fixups */
	v1 = (VexGuestAMD64State*)regs_pre;
	v2 = (VexGuestAMD64State*)regs->getConcreteBuf();

	/* clobbered by kernel; rcx = rip, r11 = rflags */
	v1->guest_RCX = v2->guest_RCX;
	v1->guest_R11 = v2->guest_R11;

	v2->guest_RIP = v1->guest_RIP;
	v1->guest_CC_OP = v2->guest_CC_OP;
	v1->guest_CC_DEP1 = v2->guest_CC_DEP1;
	v1->guest_CC_DEP2 = v2->guest_CC_DEP2;
	v1->guest_CC_NDEP = v2->guest_CC_NDEP;
	/****************/

	std::cerr << "R11-pre: " << (void*)v1->guest_R11 << '\n';
	std::cerr << "R11-klee: " << (void*)v2->guest_R11 << '\n';


	/* -4 to ignore exit code and padding */
	if (regs->cmpConcrete(regs_pre, s.st_size-4) != 0) {
		std::cerr << "REGS_PRE:\n";
		gs->getCPUState()->print(std::cerr, regs_pre);

		std::cerr << "KLEE REGS:\n";
		gs->getCPUState()->print(std::cerr, regs->getConcreteBuf());

		for (unsigned i = 0; i < s.st_size; i++) {
			if (regs_pre[i] != regs->getConcreteBuf()[i])
				std::cerr << "Mismatch Index: " << i << '\n';
		}
		assert (0 == 1 && "DID NOT MATCH PRE REGS!");
	}

	regs->writeConcrete(regs_post, s.st_size);

	delete [] regs_pre;
	delete [] regs_post;

	return true;
}

static bool apply_ossfx(Executor* exe, ExecutionState& es, unsigned seq_nr)
{
	if (apply_ossfx_regs(exe, es, seq_nr) == false)
		return false;

	if (apply_ossfx_mem(exe, es, seq_nr) == false)
		return false;

	return true;
}

SFH_DEF_ALL(OSSFX_Load, "kmc_ossfx_load", true)
{
	if (done_ossfx) goto fail;
	if (OSSFxDir.empty()) { done_ossfx = true; goto fail; }

	/* apply concrete updates */
	std::cerr << "kmc_ossfx_load: dispatch #" << dispatched_ossfx_c << '\n';
	if (apply_ossfx(sfh->executor, state, dispatched_ossfx_c)) {
		dispatched_ossfx_c++;
		state.bindLocal(target, MK_CONST(1, 32));
		return;
	}

	done_ossfx = true;

	std::cerr << "kmc_ossfx_load: it's all over\n";
	if (OSSFxExitOnDrain) {
		TERMINATE_EXIT(sfh->executor, state);
		return;
	}
fail:
	state.bindLocal(target, MK_CONST(0, 32));
}


const struct SpecialFunctionHandler::HandlerInfo *ossfxload_hi =
	&HandlerOSSFX_Load::hinfo;
