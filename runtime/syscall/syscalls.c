#define _LARGEFILE64_SOURCE

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <klee/klee.h>
#include <valgrind/libvex_guest_amd64.h>

void* kmc_sc_regs(void*);
void kmc_sc_bad(unsigned int);
void kmc_free_run(uint64_t addr, uint64_t num_bytes);
void kmc_exit(uint64_t);
void kmc_make_range_symbolic(uint64_t, uint64_t, const char*);
void* kmc_alloc_aligned(uint64_t, const char* name);

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

static void sc_mmap(void* regfile)
{
	uint64_t	addr;
	uint64_t	len;
	void		*new_regs;

	len = klee_get_value(GET_ARG1(regfile));
	new_regs = kmc_sc_regs(regfile);

	if (GET_ARG0(regfile) == 0) {
		addr = (uint64_t)kmc_alloc_aligned(len, "mmap");
		if (addr == 0) addr = (uint64_t)MAP_FAILED;
	} else {
		addr = klee_get_value(GET_ARG0(regfile));
		klee_define_fixed_object((void*)addr, len);
	}

	sc_ret_v(new_regs, addr);
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
		sc_ret_range(kmc_sc_regs(regfile), y, z);		\
		break;

void make_sym(uint64_t addr, uint64_t len, const char* name)
{
	klee_check_memory_access((void*)addr, 1);
	addr = klee_get_value(addr);
	len = klee_get_value(len);
	kmc_make_range_symbolic(addr, len, name);
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

	switch (sys_nr) {
	case SYS_open:
		sc_ret_ge0(kmc_sc_regs(regfile));
		break;
	case SYS_brk:
		klee_warning_once("failing brk");
		sc_ret_v(regfile, -1);
		break;
	case SYS_munmap:
		sc_munmap(regfile);
		break;
	case SYS_write:
		sc_ret_or(kmc_sc_regs(regfile), -1, GET_ARG2(regfile));
		break;
	case SYS_exit:
	case SYS_exit_group: {
		uint64_t exit_code;
		exit_code = klee_get_value(GET_ARG0(regfile));
		sc_ret_v(regfile, exit_code);
		kmc_exit(exit_code);
	}
	break;

	case SYS_getgroups:
		sc_ret_range(kmc_sc_regs(regfile), -1, 2);
		make_sym(GET_ARG1(regfile), GET_ARG0(regfile), "getgroups");
		break;
	case SYS_sync:
		break;
	case SYS_umask:
		sc_ret_v(regfile, 0666);
		break;
	FAKE_SC_RANGE(tgkill, -1, 0)
	case SYS_getgid:
	case SYS_getuid:
		sc_ret_ge0(kmc_sc_regs(regfile));
		break;
	case SYS_getpid:
	case SYS_gettid:
		sc_ret_v(regfile, 1000); /* FIXME: single threaded*/
		break;
	case SYS_setgid:
	case SYS_setuid:
		sc_ret_or(kmc_sc_regs(regfile), -1, 0);
		break;
	FAKE_SC_RANGE(geteuid, 0, 1)
	FAKE_SC_RANGE(getegid, 0, 1)
	FAKE_SC_RANGE(futex, -1, 1)
	FAKE_SC_RANGE(fcntl, -1, 1)
	FAKE_SC(fadvise64)
	FAKE_SC(rt_sigaction)
	FAKE_SC(rt_sigprocmask)
	case SYS_pread64:
	case SYS_read: {
		uint64_t len = klee_get_value(GET_ARG2(regfile));

		new_regs = kmc_sc_regs(regfile);
		if ((int64_t)GET_RAX(new_regs) == -1) {
			break;
		}
		sc_ret_v(new_regs, len);
		make_sym(GET_ARG1(regfile), len, "readbuf");
	}
	break;

	case SYS_sched_setaffinity:
	case SYS_sched_getaffinity:
		sc_ret_v(regfile, -1);
		break;
	FAKE_SC_RANGE(access, -1, 0)
	case SYS_newfstatat: { /* for du */
		new_regs = kmc_sc_regs(regfile);
		if ((int64_t)GET_RAX(new_regs) == -1) {
			break;
		}
		sc_ret_v(new_regs, 0);
		make_sym(GET_ARG2(regfile), sizeof(struct stat), "newstatbuf");
	}
	break;

	case SYS_lseek:
		klee_warning_once("lseek [-1, 4096]");
		sc_ret_range(kmc_sc_regs(regfile), -1, 4096);
		break;
	case SYS_prctl:
		sc_ret_v(regfile, -1);
		break;
	case SYS_ioctl:
		sc_ret_ge0(kmc_sc_regs(regfile));
		break;
	case SYS_fstat:
	case SYS_lstat:
	case SYS_stat: {
		new_regs = kmc_sc_regs(regfile);
		if ((int64_t)GET_RAX(new_regs) == -1) {
			break;
		}
		sc_ret_v(new_regs, 0);
		make_sym(GET_ARG1(regfile), sizeof(struct stat), "statbuf");
		break;
	}
	case SYS_uname:
		sc_ret_v(regfile, -1);
		klee_warning_once("failing uname");
		break;
	case SYS_writev:
		sc_ret_ge0(kmc_sc_regs(regfile));
		break;

	case SYS_getcwd: {
		uint64_t addr = klee_get_value(GET_ARG0(regfile));

		new_regs = kmc_sc_regs(regfile);
		if (	GET_RAX(new_regs) >= 1 &&
			GET_RAX(new_regs) < GET_ARG1(regfile))
		{
			make_sym(GET_ARG0(regfile), GET_ARG1(regfile), "cwdbuf");
			((char*)addr)[GET_RAX(new_regs)] = '\0';
		} else {
			sc_ret_v(new_regs, -1);
		}
		break;
	}
	case SYS_sched_getscheduler:
		klee_warning_once("Pure symbolic on sched_getscheduler");
		kmc_sc_regs(regfile);
		break;
	case SYS_sched_getparam:
		klee_warning_once("Blindly OK'd sched_getparam");
		sc_ret_v(regfile, 0);
		break;
	case SYS_close:
		sc_ret_v(regfile, 0);
		break;
	case SYS_dup:
		sc_ret_ge0(kmc_sc_regs(regfile));
		break;
	case SYS_setrlimit:
		sc_ret_or(kmc_sc_regs(regfile), -1, 0);
		break;
	case SYS_getrlimit:
		make_sym(GET_ARG1(regfile), sizeof(struct rlimit), "getrlimit");
		sc_ret_v(regfile, 0);
		break;
	case SYS_getrusage:
		sc_ret_v(regfile, 0);
		make_sym(GET_ARG1(regfile), sizeof(struct rusage), "getrusage");
		break;
	case SYS_getdents:
		new_regs = kmc_sc_regs(regfile);
		sc_ret_or(new_regs, 0, -1);
		if ((int64_t)GET_RAX(new_regs) == -1)
			break;
		make_sym(GET_ARG1(regfile), GET_ARG2(regfile), "getdents");
		break;
	FAKE_SC(unlink)
	FAKE_SC(fchmod)
	FAKE_SC(fchown)
	FAKE_SC(utimensat)
	case SYS_nanosleep: {
		uint64_t	dst_addr = klee_get_value(GET_ARG1(regfile));
		if (dst_addr != 0) {
			make_sym(
				dst_addr,
				sizeof(struct timespec),
				"nanosleep");
		}
		sc_ret_v(regfile, 0);
		break;
	}
	UNIMPL_SC(select);
	UNIMPL_SC(poll)
	UNIMPL_SC(clone)
	case SYS_setsockopt:
		sc_ret_v(regfile, 0);
		break;
	case SYS_recvfrom:
		make_sym(GET_ARG1(regfile), GET_ARG2(regfile), "recvfrom_buf");
		if (GET_ARG4(regfile))
			make_sym(
				GET_ARG4(regfile),
				sizeof(struct sockaddr_in),
				"recvfrom_sa");
		if (GET_ARG5(regfile) != 0) {
			*((socklen_t*)GET_ARG5(regfile)) = sizeof(struct sockaddr_in);
		}
		sc_ret_v(regfile, GET_ARG2(regfile));
		break;
	case SYS_sendto:
		sc_ret_or(kmc_sc_regs(regfile), -1, GET_ARG2(regfile));
		break;
	case SYS_bind:
		sc_ret_or(kmc_sc_regs(regfile), 0, -1);
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
		sc_ret_or(kmc_sc_regs(regfile), -1, 0);
		break;
	case SYS_epoll_create:
		klee_warning_once("phony epoll_creat call");
		new_regs = kmc_sc_regs(regfile);
		if ((int64_t)GET_RAX(new_regs) == -1)
			break;
		klee_assume(GET_RAX(new_regs) > 3 && GET_RAX(new_regs) < 4096);
		break;

	case SYS_getsockname:
		new_regs = kmc_sc_regs(regfile);
		if ((int64_t)GET_RAX(new_regs) == -1)
			break;

		klee_assume(GET_RAX(new_regs) == 0);
		make_sym(GET_ARG1(regfile),
			sizeof(struct sockaddr_in),
			"getsockname");
		*((socklen_t*)GET_ARG2(regfile)) = sizeof(struct sockaddr_in);
		break;

	case SYS_pipe2:
	case SYS_pipe:
		klee_warning_once("phony pipe");
		sc_ret_or(kmc_sc_regs(regfile), -1, 0);
		break;

	case SYS_mmap:
		sc_mmap(regfile);
		break;
	case SYS_socket:
		klee_warning_once("phony socket call");
		sc_ret_range(kmc_sc_regs(regfile), -1, 4096);
		break;
	case SYS_fchdir:
	case SYS_chdir:
		klee_warning_once("phony chdir");
		sc_ret_v(regfile, 0);
		break;
	case SYS_klee:
		sc_klee(regfile);
		break;
	UNIMPL_SC(readlinkat)
	UNIMPL_SC(mremap)
	case SYS_creat:
		klee_warning_once("phony creat call");
		new_regs = kmc_sc_regs(regfile);
		if ((int64_t)GET_RAX(new_regs) == -1)
			break;
		klee_assume(GET_RAX(new_regs) > 3 && GET_RAX(new_regs) < 4096);
	break;
	case SYS_readlink:
	{
		/* keep the string short since we're pure symbolic now */
		/* In the future, use system information to satisfy this */
		uint64_t	addr  = klee_get_value(GET_ARG1(regfile));
		klee_warning_once("bogus readlink");
		new_regs = kmc_sc_regs(regfile);
		klee_assume(GET_ARG2(regfile) >= 2);
		sc_ret_range(new_regs, 1, 2);
		make_sym(addr, GET_ARG2(regfile), "readlink");
		((char*)addr)[GET_ARG2(new_regs)] = '\0';
	}
	break;

	case SYS_times:
		kmc_sc_regs(regfile);
		make_sym(
			klee_get_value(GET_ARG0(regfile)),
			sizeof(struct tms),
			"times");
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

	return jmpptr;
}