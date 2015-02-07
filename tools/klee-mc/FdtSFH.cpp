#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ExecutorVex.h"
#include "FdtSFH.h"

using namespace klee;

typedef	std::map<std::string, off_t>	snapshot_map;
static snapshot_map			g_snapshots;

static const unsigned int NUM_HANDLERS = 6;
static SpecialFunctionHandler::HandlerInfo hInfo[NUM_HANDLERS] =
{
#define add(name, h, ret) {	\
	name, 			\
	&Handler##h::create,	\
	false, ret, false }
	add("sc_concrete_file_snapshot", SCConcreteFileSnapshot, true),
	add("sc_concrete_file_size", SCConcreteFileSize, true),
	add("sc_get_cwd", SCGetCwd, true),
	add("pthread_mutex_lock", DummyThread, true),
	add("pthread_mutex_unlock", DummyThread, true),
	add("pthread_cond_broadcast", DummyThread, true),
#undef add
};

FdtSFH::FdtSFH(Executor* e) : SyscallSFH(e) {}

void FdtSFH::bind(void)
{
	SyscallSFH::bind();
	SpecialFunctionHandler::bind((HandlerInfo*)&hInfo, NUM_HANDLERS);
}

void FdtSFH::prepare(void)
{
	SyscallSFH::prepare();
	SpecialFunctionHandler::prepare((HandlerInfo*)&hInfo, NUM_HANDLERS);
}

SFH_DEF_HANDLER(DummyThread)
{
	state.bindLocal(target, ConstantExpr::create(0, 64));
}

SFH_DEF_HANDLER(SCGetCwd)
{
	//TODO: write something?
	char buf[PATH_MAX], *ret;
	ret = getcwd(buf, PATH_MAX);
	(void)ret;
	assert (0 == 1 && "TODO");
}

SFH_DEF_HANDLER(SCConcreteFileSize)
{
	//TODO: CHK something?
	ConstantExpr	*path_ce, *size_ce;
	unsigned char	*buf;

	path_ce = dyn_cast<ConstantExpr>(args[0]);
	size_ce = dyn_cast<ConstantExpr>(args[1]);

	unsigned int		len_in;
	buf = sfh->readBytesAtAddress(
		state, path_ce, size_ce->getZExtValue() + 1, len_in, -1);
	std::string path = (char*)buf;

	snapshot_map::iterator i = g_snapshots.find(path);
	if(i == g_snapshots.end()) {
		state.bindLocal(target, ConstantExpr::create(-1, 64));
		return;
	}
	state.bindLocal(target, ConstantExpr::create(i->second, 64));
	std::cerr << "sized " << path << std::endl;
}

SFH_DEF_HANDLER(SCConcreteFileSnapshot)
{
	//TODO: CHK something?
	//TODO: nice to be able to add it to all state in some safe location?
	
	ConstantExpr		*path_ce, *size_ce;
	unsigned char		*buf;
	unsigned int		len_in;


	path_ce = dyn_cast<ConstantExpr>(args[0]);
	size_ce = dyn_cast<ConstantExpr>(args[1]);
	buf = sfh->readBytesAtAddress(
		state, path_ce, size_ce->getZExtValue() + 1, len_in, -1);
	std::string path = (char*)buf;
	
	long result = open(path.c_str(), O_RDONLY);
	if(result < 0) {
		state.bindLocal(target, ConstantExpr::create(-errno, 64));
		return;
	}
	int fd = result;

	struct stat st;
	result = fstat(fd, &st);
	if(result < 0) {
		close(fd);
		state.bindLocal(target, ConstantExpr::create(-errno, 64));
		return;
	}

	void* addr = mmap(NULL, st.st_size, PROT_READ, MAP_FILE | MAP_PRIVATE, fd, 0);
	close(fd);
	if(addr == MAP_FAILED) {
		state.bindLocal(target, ConstantExpr::create(-errno, 64));
		return;
	}

	MemoryObject* mo;
	// XXX FIXME:
	// mo = sfh->executor->addExternalObject(state, addr, st.st_size, true);
	assert (0 == 1 && "XXX ^^^ FIXME FIXME GLOBAL REVAMP");
	g_snapshots.insert(std::make_pair(path, st.st_size));
	std::cerr << "new file fork " << path << std::endl;
	state.bindLocal(target, ConstantExpr::create((intptr_t)mo->address, 64));
}
