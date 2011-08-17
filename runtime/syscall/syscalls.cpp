// i don't know how to do this with the makefiles... they try to build
// a native library which fails on libcxx... so this makes it build cleanly
#ifdef __llvm__

#define _LARGEFILE64_SOURCE
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <ustat.h>
#include <sys/syscall.h>
#include <klee/klee.h>
#include <grp.h>
#include <valgrind/libvex_guest_amd64.h>

#include "breadcrumb.h"
#include "kmc.h"
#include "fdt.h"

extern "C" void* sc_enter(void* regfile, void* jmpptr);

#define FAILURE_RATE	4
struct fail_counters
{
	unsigned int	fc_read;
	unsigned int	fc_stat;
	unsigned int	fc_write;
} fail_c;

// arg0, arg1, ...
// %rdi, %rsi, %rdx, %r10, %r8 and %r9a

#define GET_RAX(x)	((VexGuestAMD64State*)x)->guest_RAX
#define GET_ARG0(x)	((VexGuestAMD64State*)x)->guest_RDI
#define GET_ARG1(x)	((VexGuestAMD64State*)x)->guest_RSI
#define GET_ARG2(x)	((VexGuestAMD64State*)x)->guest_RDX
#define GET_ARG3(x)	((VexGuestAMD64State*)x)->guest_R10
#define GET_ARG4(x)	((VexGuestAMD64State*)x)->guest_R8
#define GET_ARG5(x)	((VexGuestAMD64State*)x)->guest_R9
#define GET_SYSNR(x)	GET_RAX(x)
static uint64_t GET_ARG(void* x, int y)
{
	switch (y) {
	case 0: return GET_ARG0(x);
	case 1: return GET_ARG1(x);
	case 2: return GET_ARG2(x);
	case 3: return GET_ARG3(x);
	case 4: return GET_ARG4(x);
	case 5: return GET_ARG5(x);
	}
	return -1;
}

static void* sc_new_regs(void* r)
{
	void	*ret = kmc_sc_regs(r);
	SC_BREADCRUMB_FL_OR(BC_FL_SC_NEWREGS);
	return ret;
}

static void sc_ret_le0(void* regfile)
{
	int64_t	rax = GET_RAX(regfile);
	klee_assume(rax <= 0);
}

static void sc_ret_ge0(void* regfile)
{
	int64_t	rax = GET_RAX(regfile);
	klee_assume(rax >= 0);
}

static void sc_ret_v(void* regfile, uint64_t v1)
{
	GET_RAX(regfile) = v1;
	klee_assume(GET_RAX(regfile) == v1);
}

static void sc_ret_or(void* regfile, uint64_t v1, uint64_t v2)
{
	uint64_t	rax = GET_RAX(regfile);
	klee_assume(rax == v1 || rax == v2);
}

/* inclusive */
static void sc_ret_range(void* regfile, int64_t lo, int64_t hi)
{
	int64_t	rax;
	if ((hi - lo) == 1) {
		sc_ret_or(regfile, lo, hi);
		return;
	}
	rax = GET_RAX(regfile);
	klee_assume(rax >= lo && rax <= hi);
}

static void* sc_mmap_addr(
	void* regfile,
	void* addr, uint64_t len)
{
	int	is_himem;

	is_himem = (((intptr_t)addr & ~0x7fffffffffffULL) != 0);
	if (!is_himem) {
		/* not highmem, use if we've got it.. */
		addr = (void*)concretize_u64(GET_ARG0(regfile));
		klee_define_fixed_object(addr, len);
		return addr;
	}

	/* can never satisfy a hi-mem request */
	if (GET_ARG2(regfile) & MAP_FIXED) {
		/* can never fixed map hi-mem */
		return MAP_FAILED;
	}

	/* toss back whatever */
	addr = kmc_alloc_aligned(len, "mmap", false);
	if (addr == NULL) addr = MAP_FAILED;
	return addr;
}

static void* sc_mmap(void* regfile)
{
	uint64_t	addr;
	uint64_t	len;
	void		*new_regs;

	len = GET_ARG1(regfile);
	new_regs = sc_new_regs(regfile);

	if (len >= (uintptr_t)0x10000000 || len < 0) {
		addr = (uint64_t)MAP_FAILED;
	} else if (GET_ARG0(regfile) == 0) {
		len = concretize_u64(GET_ARG1(regfile));
		addr = (uint64_t)kmc_alloc_aligned(len, "mmap", false);
		if (addr == 0) addr = (uint64_t)MAP_FAILED;
	} else {
		len = concretize_u64(GET_ARG1(regfile));
		addr = (uint64_t)sc_mmap_addr(regfile, (void*)addr, len);
	}

	sc_ret_v(new_regs, addr);
	return new_regs;
}

static void sc_munmap(void* regfile)
{
	uint64_t	addr, num_bytes;

	addr = klee_get_value(GET_ARG0(regfile));
	num_bytes = klee_get_value(GET_ARG1(regfile));
	kmc_free_run(addr, num_bytes);

	/* always succeeds, don't bother with new regctx */
	sc_ret_v(regfile, 0);
}

#define UNIMPL_SC(x)						\
	case SYS_##x:						\
		klee_report_error(				\
			__FILE__, __LINE__,			\
			"Unimplemented syscall "#x, "sc.err");	\
		break;
#define FAKE_SC(x)							\
	case SYS_##x:							\
		klee_warning_once("Faking successful syscall "#x);	\
		sc_ret_v(regfile, 0);					\
		break;
#define FAKE_SC_RANGE(x,y,z)						\
	case SYS_##x:							\
		klee_warning_once("Faking range syscall "#x);		\
		sc_ret_range(sc_new_regs(regfile), y, z);		\
		break;

void make_sym(uint64_t addr, uint64_t len, const char* name)
{
	klee_check_memory_access((void*)addr, 1);

	klee_assume(addr == klee_get_value(addr));
	klee_assume(len == klee_get_value(len));

	kmc_make_range_symbolic(addr, len, name);
	sc_breadcrumb_add_ptr((void*)addr, len);
}

void make_sym_by_arg(
	void	*regfile,
	uint64_t arg_num,
	uint64_t len, const char* name)
{
	uint64_t	addr;

	addr = GET_ARG(regfile, arg_num);
	klee_assume (addr == klee_get_value(addr));
	klee_assume (len == klee_get_value(len));

	klee_check_memory_access((void*)addr, 1);

	kmc_make_range_symbolic(addr, len, name);
	sc_breadcrumb_add_argptr(arg_num, 0, len);
}

static void sc_poll(void* regfile)
{
	struct pollfd	*fds;
	uint64_t	poll_addr;
	unsigned int	i, nfds;

	poll_addr = klee_get_value(GET_ARG0(regfile));
	fds = (struct pollfd*)poll_addr;
	nfds = klee_get_value(GET_ARG1(regfile));

	for (i = 0; i < nfds; i++) {
		klee_check_memory_access(&fds[i], sizeof(struct pollfd));
		fds[i].revents = klee_get_value(fds[i].events);
	}

	sc_ret_v(regfile, nfds);
}

static void sc_klee(void* regfile)
{
	unsigned int	sys_klee_nr;

	sys_klee_nr = GET_ARG0(regfile);
	switch(sys_klee_nr) {
	case KLEE_SYS_REPORT_ERROR:
		klee_report_error(
			(const char*)GET_ARG1(regfile),
			GET_ARG2(regfile),
			(const char*)klee_get_value(GET_ARG3(regfile)),
			(const char*)klee_get_value(GET_ARG4(regfile)));
		break;
	default:
		klee_report_error(
			__FILE__,
			__LINE__,
			"Unsupported SYS_klee syscall",
			"kleesc.err");
		break;
	}
}

void* sc_enter(void* regfile, void* jmpptr)
{
	void			*new_regs;
	unsigned int		sys_nr;

	sys_nr = GET_SYSNR(regfile);
	if (klee_is_symbolic(sys_nr)) {
		klee_warning_once("Resolving symbolic syscall nr");
		sys_nr = klee_get_value(sys_nr);
	}

	sc_breadcrumb_reset();

	switch (sys_nr) {
	case SYS_getpeername:
		new_regs = sc_new_regs(regfile);
		if ((intptr_t)GET_RAX(new_regs) == -1)
			break;

		klee_assume(GET_RAX(new_regs) == 0);
		make_sym_by_arg(
			regfile,
			1,
			klee_get_value(*((socklen_t*)GET_ARG2(regfile))),
			"getpeeraddr");

		break;
	case SYS_listen:
		sc_ret_v(regfile, 0);
		break;
	case SYS_mlock:
		sc_ret_v(regfile, 0);
		break;
	case SYS_munlock:
		sc_ret_v(regfile, 0);
		break;
	case SYS_openat:
		new_regs = sc_new_regs(regfile);
		sc_ret_v(new_regs, -1);
		break;
	case SYS_open:
		new_regs = sc_new_regs(regfile);
		sc_ret_v(new_regs, vfs.open((char*)GET_ARG0(regfile),
		 	(int)GET_ARG1(regfile), (int)GET_ARG2(regfile)));
		break;
	case SYS_brk:
		klee_warning_once("failing brk");
		sc_ret_v(regfile, -1);
		break;
	case SYS_munmap:
		sc_munmap(regfile);
		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
		break;
	case SYS_pwrite64:
		new_regs = sc_new_regs(regfile);
		sc_ret_v(new_regs, 	
			fdt.alwaysGetFile((long)GET_ARG0(regfile))->pwrite(
				(void*)GET_ARG1(regfile), 
				(size_t)GET_ARG2(regfile),
				(off_t)GET_ARG2(regfile)));
		break;

	case SYS_write: {
		new_regs = sc_new_regs(regfile);
		sc_ret_v(new_regs, 	
			fdt.alwaysGetFile((long)GET_ARG0(regfile))->write(
				(void*)GET_ARG1(regfile), 
				(size_t)GET_ARG2(regfile)));
		break;
	}
	break;
	case SYS_exit:
	case SYS_exit_group: {
		uint64_t exit_code;
		exit_code = klee_get_value(GET_ARG0(regfile));
		sc_ret_v(regfile, exit_code);
		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
		sc_breadcrumb_commit(sys_nr, exit_code);
		kmc_exit(exit_code);
	}
	break;

	case SYS_tgkill:
		if (GET_ARG2(regfile) == SIGABRT) {
			sc_ret_v(regfile, SIGABRT);
			SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
			sc_breadcrumb_commit(sys_nr, SIGABRT);
			kmc_exit(SIGABRT);
		} else {
			sc_ret_or(sc_new_regs(regfile), 0, -1);
		}
		break;

	case SYS_sync:
		break;
	case SYS_umask:
		sc_ret_v(regfile, 0666);
		break;
	case SYS_getgid:
	case SYS_getuid:
		sc_ret_ge0(sc_new_regs(regfile));
		break;
	case SYS_getpid:
	case SYS_gettid:
		sc_ret_v(regfile, 1000); /* FIXME: single threaded*/
		break;
	case SYS_setgid:
	case SYS_setuid:
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;
	FAKE_SC_RANGE(geteuid, 0, 1)
	FAKE_SC_RANGE(getegid, 0, 1)
	FAKE_SC_RANGE(futex, -1, 1)
	FAKE_SC_RANGE(fcntl, -1, 1)
	FAKE_SC(fadvise64)
	FAKE_SC(rt_sigaction)
	FAKE_SC(rt_sigprocmask)
	case SYS_pread64:
		new_regs = sc_new_regs(regfile);
		sc_ret_v(new_regs, 	
			fdt.alwaysGetFile((long)GET_ARG0(regfile))->pread(
				(void*)GET_ARG1(regfile), 
				(size_t)GET_ARG2(regfile),
				(off_t)GET_ARG3(regfile)));
		break;
	
	case SYS_read: {
		new_regs = sc_new_regs(regfile);
		sc_ret_v(new_regs, 	
			fdt.alwaysGetFile((long)GET_ARG0(regfile))->read(
				(void*)GET_ARG1(regfile), 
				(size_t)GET_ARG2(regfile)));
		break;
	}
	break;

	case SYS_getgroups:
		if ((intptr_t)GET_ARG0(regfile) < 0) {
			sc_ret_v(regfile, -1);
			break;
		}

		if ((intptr_t)GET_ARG0(regfile) == 0) {
			sc_ret_v(regfile, 2);
			break;
		}

		make_sym_by_arg(regfile, 1, GET_ARG0(regfile), "getgroups");
		sc_ret_v(regfile, GET_ARG0(regfile));
		break;
	case SYS_sched_setaffinity:
	case SYS_sched_getaffinity:
		sc_ret_v(regfile, -1);
		break;
	case SYS_access:
		new_regs = sc_new_regs(regfile);
		sc_ret_v(new_regs, 
			vfs.access(
				(char*)GET_ARG0(regfile), 
				(int)GET_ARG1(regfile)));
		break;
	case SYS_newfstatat: { /* for du */
		new_regs = sc_new_regs(regfile);
		if ((int64_t)GET_RAX(new_regs) == -1) {
			break;
		}
		klee_assume(GET_RAX(new_regs) == 0);
		sc_ret_v(new_regs, 0);
		make_sym_by_arg(regfile, 2, sizeof(struct stat),
			"newstatbuf");
	}
	break;

	case SYS_lseek:
		klee_warning_once("lseek [-1, 4096]");
		sc_ret_range(sc_new_regs(regfile), -1, 4096);
		break;
	case SYS_prctl:
		sc_ret_v(regfile, -1);
		break;
	case SYS_ioctl:
		sc_ret_ge0(sc_new_regs(regfile));
		break;
	case SYS_fstat:
		new_regs = sc_new_regs(regfile);
		sc_ret_v(new_regs, 	
			fdt.alwaysGetFile((long)GET_ARG0(regfile))->stat(
				(struct stat*)GET_ARG1(regfile)));
		klee_warning("garl");
		break;
	case SYS_lstat:
	case SYS_stat:
		new_regs = sc_new_regs(regfile);
		sc_ret_v(new_regs, 	
			vfs.stat((char*)GET_ARG0(regfile),
				(struct stat*)GET_ARG1(regfile)));
		break;
	case SYS_uname:
		sc_ret_v(regfile, -1);
		klee_warning_once("failing uname");
		break;
	case SYS_writev:
		sc_ret_ge0(sc_new_regs(regfile));
		break;

	case SYS_getcwd: {
		uint64_t addr = klee_get_value(GET_ARG0(regfile));

		new_regs = sc_new_regs(regfile);
		if (	GET_RAX(new_regs) >= 1 &&
			GET_RAX(new_regs) < GET_ARG1(regfile))
		{
			make_sym_by_arg(
				regfile,
				0,
				klee_get_value(GET_ARG1(regfile)),
				"cwdbuf");
			((char*)addr)[GET_RAX(new_regs)] = '\0';
		} else {
			sc_ret_v(new_regs, -1);
		}
		break;
	}
	case SYS_sched_getscheduler:
		klee_warning_once("Pure symbolic on sched_getscheduler");
		sc_new_regs(regfile);
		break;
	case SYS_sched_getparam:
		klee_warning_once("Blindly OK'd sched_getparam");
		sc_ret_v(regfile, 0);
		break;
	case SYS_close:
		new_regs = sc_new_regs(regfile);
		//TODO: should return this result
		fdt.closeFile((long)GET_ARG0(regfile));
		sc_ret_v(new_regs, 0);
		break;
	case SYS_dup:
		new_regs = sc_new_regs(regfile);
		sc_ret_v(new_regs, fdt.dupFile((long)GET_ARG0(regfile)));
		break;
	case SYS_dup2:
		new_regs = sc_new_regs(regfile);
		sc_ret_v(new_regs, 
			fdt.dupFile((long)GET_ARG0(regfile),
			(long)GET_ARG1(regfile)));
		break;
	case SYS_setrlimit:
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;
	case SYS_getrlimit:
		make_sym_by_arg(regfile, 1,sizeof(struct rlimit),
			"getrlimit");
		sc_ret_v(regfile, 0);
		break;
	case SYS_getrusage:
		sc_ret_v(regfile, 0);
		make_sym_by_arg(regfile, 1, sizeof(struct rusage),
			"getrusage");
		break;
	case SYS_getdents:
		new_regs = sc_new_regs(regfile);
		sc_ret_or(new_regs, 0, -1);
		if ((int64_t)GET_RAX(new_regs) == -1)
			break;
		make_sym_by_arg(regfile, 1, GET_ARG2(regfile), "getdents");
		break;
	FAKE_SC(unlink)
	FAKE_SC(fchmod)
	FAKE_SC(fchown)
	FAKE_SC(utimensat)
	case SYS_nanosleep: {
		uint64_t	dst_addr = klee_get_value(GET_ARG1(regfile));
		if (dst_addr != 0) {
			klee_assume(GET_ARG1(regfile) == dst_addr);
			make_sym(
				dst_addr,
				sizeof(struct timespec),
				"nanosleep");
		}
		sc_ret_v(regfile, 0);
		break;
	}
	UNIMPL_SC(select);
	UNIMPL_SC(clone);

	case SYS_recvmsg:
		new_regs = sc_new_regs(regfile);
		sc_ret_v(new_regs, 	
			fdt.alwaysGetFile((long)GET_ARG0(regfile))->recvmsg(
				(struct msghdr *)GET_ARG1(regfile), 
				(int)GET_ARG2(regfile)));
		break;

	case SYS_setsockopt:
		sc_ret_v(regfile, 0);
		break;
	case SYS_recvfrom:
		new_regs = sc_new_regs(regfile);
		sc_ret_v(new_regs, 	
			fdt.alwaysGetFile((long)GET_ARG0(regfile))->recvfrom(
				(void *)GET_ARG1(regfile), 
				(size_t)GET_ARG2(regfile),
				(int)GET_ARG3(regfile),
				(struct sockaddr*)GET_ARG4(regfile),
				(socklen_t*)GET_ARG5(regfile)));
		break;
	case SYS_sendto:
		new_regs = sc_new_regs(regfile);
		sc_ret_v(new_regs, 	
			fdt.alwaysGetFile((long)GET_ARG0(regfile))->sendto(
				(void *)GET_ARG1(regfile), 
				(size_t)GET_ARG2(regfile),
				(int)GET_ARG3(regfile),
				(struct sockaddr*)GET_ARG4(regfile),
				(socklen_t)GET_ARG5(regfile)));
		break;
	case SYS_bind:
		sc_ret_or(sc_new_regs(regfile), 0, -1);
		break;
	case SYS_chmod:
		klee_warning_once("phony chmod");
		sc_ret_v(regfile, 0);
		break;
	case SYS_mkdir:
		klee_warning_once("phony mkdir");
		sc_ret_v(regfile, 0);
		break;
	case SYS_connect:
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;
	case SYS_epoll_create:
		klee_warning_once("phony epoll_creat call");
		new_regs = sc_new_regs(regfile);
		if ((int64_t)GET_RAX(new_regs) == -1)
			break;
		sc_ret_v(new_regs, 
			fdt.newFile(new SymbolicFD("epoll_create")));
		break;

	case SYS_getsockname:
		new_regs = sc_new_regs(regfile);
		if ((int64_t)GET_RAX(new_regs) == -1)
			break;

		klee_assume(GET_RAX(new_regs) == 0);
		make_sym(GET_ARG1(regfile),
			sizeof(struct sockaddr_in),
			"getsockname");
		*((socklen_t*)GET_ARG2(regfile)) = sizeof(struct sockaddr_in);
		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
		break;

	case SYS_pipe2:
		klee_warning_once("even phonier pipe2 as a...");
	case SYS_pipe:{
		klee_warning_once("phony pipe");
		new_regs = sc_new_regs(regfile);
		long socket0 = fdt.newFile(new SymbolicSocket());
		long socket1 = fdt.newFile(new SymbolicSocket());
		if(socket0 < 0) {
			fdt.closeFile(socket1);
			sc_ret_v(new_regs, socket0);
			break;
		}
		if(socket1 < 0) {
			fdt.closeFile(socket0);
			sc_ret_v(new_regs, socket1);
			break;
		}
		int* pipes = (int *)GET_ARG0(regfile);
		pipes[0] = socket0;
		pipes[1] = socket1;
		sc_ret_v(new_regs, 0);
		break;
	}

	case SYS_mmap: {
		if(!(GET_ARG3(regfile) & MAP_ANONYMOUS)) {
			FD* file = fdt.getFile((long)GET_ARG4(regfile));
			new_regs = sc_new_regs(regfile);
			sc_ret_v(new_regs, file->mmap(
				(void *)GET_ARG0(regfile), 
				(size_t)GET_ARG1(regfile),
				(int)GET_ARG2(regfile),
				(int)GET_ARG3(regfile),
				(off_t)GET_ARG5(regfile)));
		} else {
			new_regs = sc_mmap(regfile);
			SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
			sc_breadcrumb_commit(sys_nr, GET_RAX(new_regs));
			goto already_logged;
		}
		break;
	}
	case SYS_socket:
		klee_warning_once("phony socket call");
		new_regs = sc_new_regs(regfile);
		if ((int64_t)GET_RAX(new_regs) == -1)
			break;
		sc_ret_v(new_regs, fdt.newFile(new SymbolicSocket()));
		break;
	case SYS_fchdir:
		klee_warning_once("phony chdir");
		new_regs = sc_new_regs(regfile);
		sc_ret_v(regfile, vfs.chdir(fdt.alwaysGetFile(
			(long)GET_ARG0(regfile))->identifier_.c_str()));
		break;
	case SYS_chdir:
		klee_warning_once("phony chdir");
		new_regs = sc_new_regs(regfile);
		sc_ret_v(regfile, vfs.chdir((char *)GET_ARG0(regfile)));
		break;
	case SYS_klee:
		sc_klee(regfile);
		break;
	UNIMPL_SC(readlinkat)
	UNIMPL_SC(mremap)
	case SYS_creat:
		klee_warning_once("phony creat call");
		new_regs = sc_new_regs(regfile);
		sc_ret_v(new_regs, vfs.open((char*)GET_ARG0(regfile),
			O_CREAT|O_WRONLY|O_TRUNC, (int)GET_ARG1(regfile)));
	break;
	case SYS_readlink:
	{
		/* keep the string short since we're pure symbolic now */
		/* In the future, use system information to satisfy this */
		uint64_t	addr  = klee_get_value(GET_ARG1(regfile));
		klee_warning_once("bogus readlink");
		new_regs = sc_new_regs(regfile);
		klee_assume(GET_ARG2(regfile) >= 2);
		sc_ret_range(new_regs, 1, 2);
		make_sym(addr, GET_ARG2(regfile), "readlink");
		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
		((char*)addr)[GET_ARG2(new_regs)] = '\0';
	}
	break;

	case SYS_poll:
		sc_poll(regfile);
		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
		break;

	case SYS_times:
		sc_new_regs(regfile);
		make_sym(
			klee_get_value(GET_ARG0(regfile)),
			sizeof(struct tms),
			"times");
	break;

	case SYS_ustat:
		/* int ustat(dev, ubuf) */
		new_regs = sc_new_regs(regfile);
		if ((intptr_t)GET_RAX(new_regs) == -1)
			break;

		klee_assume(GET_RAX(new_regs) == 0);
		make_sym_by_arg(
			regfile,
			1,
			sizeof(struct ustat),
			"ustatbuf");
		break;

	default:
		kmc_sc_bad(sys_nr);
		klee_report_error(
			__FILE__,
			__LINE__,
			"Unknown Syscall",
			"sc.err");
		break;
	}

	if (sc_breadcrumb_is_newregs()) {
		/* ret value is stored in ktest regctx */
		sc_breadcrumb_commit(sys_nr, 0);
	} else {
		/* ret value was written concretely */
		sc_breadcrumb_commit(sys_nr, GET_RAX(regfile));
	}

already_logged:
	return jmpptr;
}
#endif
