#define _LARGEFILE64_SOURCE

#include <sys/vfs.h>
#include <sys/sysinfo.h>
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
#include <unistd.h>
#include <limits.h>
#include <ustat.h>
#include <sys/syscall.h>
#include <klee/klee.h>
#include <grp.h>

#include "file.h"
#include "syscalls.h"
#include "concrete_fd.h"
#include "breadcrumb.h"

//#define USE_SYS_FAILURE

void* kmc_sc_regs(void*);
void kmc_sc_bad(unsigned int);
void kmc_free_run(uint64_t addr, uint64_t num_bytes);
void kmc_exit(uint64_t);
void kmc_make_range_symbolic(uint64_t, uint64_t, const char*);
void* kmc_alloc_aligned(uint64_t, const char* name);

/* klee-mc fiddles with these on init */
uint64_t	heap_begin;
uint64_t	 heap_end;
static void	*last_brk = 0;

// arg0, arg1, ...
// %rdi, %rsi, %rdx, %r10, %r8 and %r9a

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

static void sc_ret_ge0(void* regfile)
{
	ARCH_SIGN_CAST rax = GET_SYSRET_S(regfile);
	klee_assume(rax >= 0);
}

void sc_ret_or(void* regfile, uint64_t v1, uint64_t v2)
{
	ARCH_SIGN_CAST rax = GET_SYSRET(regfile);
	klee_assume(rax == (ARCH_SIGN_CAST)v1 || rax == (ARCH_SIGN_CAST)v2);
}

/* inclusive */
void sc_ret_range(void* regfile, int64_t lo, int64_t hi)
{
	ARCH_SIGN_CAST rax;
	if ((hi - lo) == 1) {
		sc_ret_or(regfile, lo, hi);
		return;
	}
	rax = GET_SYSRET_S(regfile);
	klee_assume(rax >= (ARCH_SIGN_CAST)lo && rax <= (ARCH_SIGN_CAST)hi);
}

/* IMPORTANT: this will just *allocate* some data,
 * if you want symbolic, do it after calling this */
static void* sc_mmap_addr(void* regfile, void* addr, uint64_t len)
{
	int	is_himem;

	is_himem = (((intptr_t)addr & ~0x7fffffffffffULL) != 0);
	if (!is_himem) {
		/* not highmem, use if we've got it.. */
		addr = (void*)concretize_u64(GET_ARG0(regfile));
		klee_print_expr("non-highmem define fixed addr", addr);
		klee_define_fixed_object(addr, len);
		return addr;
	}

	/* can never satisfy a hi-mem request */
	if (GET_ARG2(regfile) & MAP_FIXED) {
		/* can never fixed map hi-mem */
		return MAP_FAILED;
	}

	/* toss back whatever */
	addr = kmc_alloc_aligned(len, "mmap");
	if (addr == NULL) addr = MAP_FAILED;
	return addr;
}

static void* sc_mmap_anon(void* regfile)
{
	void		*addr;
	uint64_t	len;

	len = GET_ARG1(regfile);
	if (len <= 4096)
		len = concretize_u64(GET_ARG1(regfile));
	else if (len <= 16*1024)
		len = concretize_u64(GET_ARG1(regfile));
	else
		len = concretize_u64(GET_ARG1(regfile));

	/* mapping may be placed anywhere */
	if (GET_ARG0(regfile) == 0) {
		addr = kmc_alloc_aligned(len, "mmap");
		if (addr == NULL) addr = MAP_FAILED;
		return addr;
	}

	/* mapping has a deisred location */
	addr = sc_mmap_addr(regfile, (void*)GET_ARG0(regfile), len);
	return addr;
}

// return address of mmap
static void* sc_mmap_fd(void* regfile)
{
	void		*ret_addr;
	uint64_t	len;

	len = GET_ARG1(regfile);
	/* TODO, how should this be split? */
	if (len <= 4096) {
		len = concretize_u64(len);
	} else if (len <= 8192) {
		len = concretize_u64(len);
	} else if (len <= (16*1024)) {
		len = concretize_u64(len);
	} else {
		len = concretize_u64(len);
	}

	ret_addr = sc_mmap_anon(regfile);
	if (ret_addr == MAP_FAILED) return ret_addr;

	make_sym((uint64_t)ret_addr, len, "mmap_fd");
	return ret_addr;
}

static void* sc_mmap(void* regfile)
{
	void		*addr;
	uint64_t	len;
	void		*new_regs;

	len = GET_ARG1(regfile);
	new_regs = sc_new_regs(regfile);

	if (len >= (uintptr_t)0x10000000 || (int64_t)len <= 0) {
		addr = MAP_FAILED;
	} else if (((int)GET_ARG4(regfile)) != -1) {
		/* file descriptor mmap-- things need to be symbolic */
		addr = sc_mmap_fd(regfile);
	} else if ((GET_ARG3(regfile) & MAP_ANONYMOUS) == 0) {
		/* !fd && !anon => WTF */
		addr = MAP_FAILED;
	} else {
		addr = sc_mmap_anon(regfile);
	}

	sc_ret_v_new(new_regs, (uint64_t)addr);
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

/* number of bytes in extent we'll make symbolic before yielding */
#define SYM_YIELD_SIZE	(16*1024)

void make_sym(uint64_t addr, uint64_t len, const char* name)
{
	klee_check_memory_access((void*)addr, 1);

	addr = concretize_u64(addr);
	if (len > SYM_YIELD_SIZE)
		klee_yield();

	if (len == 0)
		return;

	len = concretize_u64(len);
	kmc_make_range_symbolic(addr, len, name);
	sc_breadcrumb_add_ptr((void*)addr, len);
}

void make_sym_by_arg(
	void	*regfile,
	uint64_t arg_num,
	uint64_t len, const char* name)
{
	uint64_t	addr;

	addr = concretize_u64(GET_ARG(regfile, arg_num));
	klee_check_memory_access((void*)addr, 1);

	if (len > SYM_YIELD_SIZE)
		klee_yield();

	if (len == 0)
		return;

	len = concretize_u64(len);
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
			(const char*)GET_ARG1_PTR(regfile),
			GET_ARG2(regfile),
			(const char*)klee_get_value(GET_ARG3(regfile)),
			(const char*)klee_get_value(GET_ARG4(regfile)));
		break;
	case KLEE_SYS_KMC_SYMRANGE:
		make_sym(
			GET_ARG1(regfile),	/* addr */
			GET_ARG2(regfile),	/* len */
			(const char*)GET_ARG3_PTR(regfile) /* name */);
		sc_ret_v(regfile, 0);
		break;
	case KLEE_SYS_ASSUME:
		klee_assume(GET_ARG1(regfile));
		break;
	case KLEE_SYS_IS_SYM:
		sc_ret_v(regfile, klee_is_symbolic(GET_ARG1(regfile)));
		break;
	case KLEE_SYS_NE:
		klee_force_ne(GET_ARG1(regfile), GET_ARG2(regfile));
		break;
	case KLEE_SYS_PRINT_EXPR:
		klee_print_expr(GET_ARG1_PTR(regfile), GET_ARG2(regfile));
		break;
	case KLEE_SYS_SILENT_EXIT:
		klee_silent_exit(GET_ARG1(regfile));
		break;
	case KLEE_SYS_SYM_RANGE_BYTES:
		sc_ret_v(
			regfile,
			klee_sym_range_bytes(GET_ARG1_PTR(regfile), GET_ARG2(regfile)));
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

#include <asm/ptrace.h>

void* sc_enter(void* regfile, void* jmpptr)
{
	struct sc_pkt		sc;
	void			*new_regs;

	sc_clear(&sc);
	sc.regfile = regfile;
	sc.pure_sys_nr = GET_SYSNR(regfile);

	if (klee_is_symbolic(sc.pure_sys_nr)) {
		klee_warning_once("Resolving symbolic syscall nr");
		sc.pure_sys_nr = concretize_u64(sc.pure_sys_nr);
	}

	sc.sys_nr = sc.pure_sys_nr;

#ifndef GUEST_ARCH_AMD64
	/* non-native architecture */
	syscall_xlate(&sc);
#endif

	sc_breadcrumb_reset();

	switch (sc.sys_nr) {
#if 0
/* tricky semantics */
	case SYS_clone: {
		// clone(pt_regs); UGH!
		struct pt_regs	*pt = (void*)GET_ARG0(regfile);
		klee_warning_once("clone() will give funny semantics");

		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET(new_regs) == 0) {
			jmpptr = pt->rip;
		} else {
			klee_assume(GET_SYSRET(new_regs) == 1001);
		}
		break;
	}
#endif
	case SYS_fork:
		new_regs = sc_new_regs(regfile);
		sc_ret_or(
			sc_new_regs(regfile),
			0 /* child */,
			1001 /* child's PID, passed to parent */);
		break;
	case SYS_getpeername:
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET_S(new_regs) == -1)
			break;

		klee_assume(GET_SYSRET(new_regs) == 0);
		make_sym_by_arg(
			regfile,
			1,
			klee_get_value(*((socklen_t*)GET_ARG2_PTR(regfile))),
			"getpeeraddr");

		break;
	case SYS_mprotect:
		klee_warning_once("ignoring mprotect()");
		sc_ret_v(regfile, -1);
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
	case SYS_brk: {
		// ptrdiff_t grow_len;

		if (last_brk == 0)
			last_brk = (void*)heap_end;
		sc_ret_v(regfile, (uintptr_t)last_brk);
		break;
	}
#if 0
		new_regs = sc_new_regs(regfile);

		/* error case -- linux returns current break */
		if (GET_SYSRET(new_regs) == (uintptr_t)last_brk) {
			sc_ret_v_new(new_regs, last_brk);
			break;
		}

		/* don't forget:
		 * heap grows forward into a stack that grows down! */
		grow_len = GET_ARG0(regfile) - (intptr_t)last_brk;

		if (grow_len == 0) {
			/* request nothing */
			klee_warning("Program requesting empty break? Weird.");
		} else if (grow_len < 0) {
			/* deallocate */
			uint64_t	dealloc_base;

			dealloc_base = (intptr_t)last_brk + grow_len;
			num_bytes = -grow_len;
			kmc_free_run(dealloc_base, num_bytes);

			last_brk = (void*)dealloc_base;
		} else {
			/* grow */
			last_brk
		}

		sc_ret_v_new(new_regs, last_brk);
		break;
#endif
	case SYS_munmap:
		sc_munmap(regfile);
		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
		break;
	case SYS_write:
#ifdef USE_SYS_FAILURE
		if (	GET_ARG0(regfile) != 1		/* never fail stdout */
			&& GET_ARG0(regfile) != 2	/* never fail stderr */
		)//	&& fail_c.fc_write % (4*FAILURE_RATE))
		{
			new_regs = sc_new_regs(regfile);
			if ((int64_t)GET_SYSRET(new_regs) == -1) {
				break;
			}
//			sc_ret_v(new_regs, concretize_u64(GET_ARG2(regfile)));
			sc_ret_v_new(new_regs, GET_ARG2(regfile));
		} else
#endif
			sc_ret_v(regfile, GET_ARG2(regfile));
//			sc_ret_v(regfile, concretize_u64(GET_ARG2(regfile)));
		break;
	case SYS_exit:
	case SYS_exit_group: {
		uint64_t exit_code;
		exit_code = klee_get_value(GET_ARG0(regfile));
		sc_ret_v(regfile, exit_code);
		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
		sc_breadcrumb_commit(&sc, exit_code);
		kmc_exit(exit_code);
	}
	break;

	case SYS_tgkill:
		if (GET_ARG2(regfile) == SIGABRT) {
			sc_ret_v(regfile, SIGABRT);
			SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
			sc_breadcrumb_commit(&sc, SIGABRT);
			kmc_exit(SIGABRT);
		} else {
			sc_ret_or(sc_new_regs(regfile), 0, -1);
		}
		break;

	case SYS_clock_getres:
		if (GET_ARG1(regfile) == 0) {
			sc_ret_v(regfile, -1);
			break;
		}
		make_sym(GET_ARG1(regfile), sizeof(struct timespec), "clock_getres");
		sc_ret_or(sc_new_regs(regfile), 0, -1);
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

	case SYS_alarm:
		sc_ret_ge0(sc_new_regs(regfile));
		break;

	case SYS_getpgrp:
	case SYS_getsid:
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

	case SYS_rt_sigprocmask:
		if (GET_ARG2(regfile)) {
			make_sym(GET_ARG2(regfile), sizeof(sigset_t), "sigset");
			sc_ret_v(regfile, 0);
		} else {
			sc_ret_or(sc_new_regs(regfile), -1, 0);
		}
		break;

	case SYS_kill:
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;

	case SYS_keyctl:
		new_regs = sc_new_regs(regfile);
		break;

	case SYS_sysinfo:
		make_sym_by_arg(regfile, 0, sizeof(struct sysinfo), "sysinfo");
		sc_ret_v(regfile, 0);
		break;

	case SYS_getgroups:
		if (GET_ARG0_S(regfile) < 0) {
			sc_ret_v(regfile, -1);
			break;
		}

		if (GET_ARG0(regfile) == 0) {
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
	FAKE_SC_RANGE(access, -1, 0)
	case SYS_newfstatat: { /* for du */
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET_S(new_regs) == -1) {
			break;
		}
		klee_assume(GET_SYSRET(new_regs) == 0);
		sc_ret_v_new(new_regs, 0);
		make_sym_by_arg(regfile, 2, sizeof(struct stat), "newstatbuf");
	}
	break;

	case SYS_clock_gettime: {
		void*	timespec = (void*)GET_ARG1(regfile);
		if (timespec == NULL) {
			sc_ret_v(regfile, -1);
			break;
		}
		make_sym(GET_ARG1(regfile), sizeof(struct timespec), "timespec");
		sc_ret_v(regfile, 0);
		break;
	}


	UNIMPL_SC(readlinkat)
	case SYS_pread64:
	case SYS_read:
	case SYS_fstat:
	case SYS_lstat:
	case SYS_creat:
	case SYS_readlink:
	case SYS_lseek:
	case SYS_stat:
	case SYS_openat:
	case SYS_open:
	case SYS_close:
		if (!file_sc(&sc))
			goto already_logged;
		break;
	case SYS_prctl:
		sc_ret_v(regfile, -1);
		break;
	case SYS_ioctl:
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET_S(new_regs) == -1)
			break;
		sc_ret_ge0(new_regs);
		break;

	case SYS_uname:
		sc_ret_v(regfile, -1);
		klee_warning_once("failing uname");
		break;
	case SYS_writev:
		sc_ret_ge0(sc_new_regs(regfile));
		break;

	FAKE_SC_RANGE(sigaltstack, -1, 0);

	case SYS_getcwd: {
		uint64_t addr = GET_ARG0(regfile);
		uint64_t len = GET_ARG1(regfile);

		if (len == 0 || addr == 0) {
			sc_ret_v(regfile, 0);
			break;
		}

		addr = concretize_u64(GET_ARG0(regfile));

		/* use managably-sized string */
		if (len > 10) len = 10;

		make_sym(addr, len, "cwdbuf");

		// XXX remember to do this on the other side!!
		((char*)addr)[len-1] = '\0';

		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
		if (addr != GET_ARG0(regfile)) {
			klee_report_error(
				__FILE__,__LINE__,
				"getcwd retmismatch. ARHGHGGHGHGHG",
				"badcwd.err");
		}

		sc_ret_v(regfile, addr);
		sc_breadcrumb_commit(&sc, addr);
		goto already_logged;
	}
	break;

	case SYS_sched_getscheduler:
		klee_warning_once("Pure symbolic on sched_getscheduler");
		sc_new_regs(regfile);
		break;
	case SYS_sched_getparam:
		klee_warning_once("Blindly OK'd sched_getparam");
		sc_ret_v(regfile, 0);
		break;
	case SYS_dup:
	case SYS_dup2:
	case SYS_dup3:
		klee_warning_once("dup is hyper-broken");
		sc_ret_ge0(sc_new_regs(regfile));
		break;
	case SYS_setrlimit:
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;
	case SYS_getrlimit:
		make_sym_by_arg(regfile, 1,sizeof(struct rlimit), "getrlimit");
		sc_ret_v(regfile, 0);
		break;
	case SYS_getrusage:
		sc_ret_v(regfile, 0);
		make_sym_by_arg(regfile, 1, sizeof(struct rusage), "getrusage");
		break;
	case SYS_getdents64:
	case SYS_getdents:
		new_regs = sc_new_regs(regfile);
		sc_ret_or(new_regs, 0, -1);
		if (GET_SYSRET_S(new_regs) == -1)
			break;
		make_sym_by_arg(regfile, 1, GET_ARG2(regfile), "getdents");
		break;
	FAKE_SC(unlink)
	FAKE_SC(fchmod)
	FAKE_SC(fchown)
	FAKE_SC(utimensat)
	case SYS_nanosleep: {
		if (GET_ARG1(regfile) != 0) {
			uint64_t dst_addr = concretize_u64(GET_ARG1(regfile));
			make_sym(
				dst_addr,
				sizeof(struct timespec),
				"nanosleep");
			sc_ret_v(regfile, -1);
			break;
		}
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;
	}

	case SYS_utime:
		sc_ret_or(sc_new_regs(regfile), 0, -1);
		break;

	case SYS_select: {
		new_regs = sc_new_regs(regfile);

		/* let all through */
		if (GET_SYSRET(new_regs) == GET_ARG0(regfile)) {
			if (GET_ARG1(regfile))
				make_sym(
					GET_ARG1(regfile),
					sizeof(fd_set), "readfds");
			if (GET_ARG2(regfile))
				make_sym(
					GET_ARG2(regfile),
					sizeof(fd_set), "writefds");
			if (GET_ARG3(regfile))
				make_sym(
					GET_ARG3(regfile),
					sizeof(fd_set), "exceptfds");

			if (GET_ARG4(regfile)) {
				make_sym(
					GET_ARG4(regfile),
					sizeof(struct timeval),
					"timeoutbuf");
			}

			sc_ret_v_new(new_regs, (int)GET_ARG0(regfile));
			break;
		}

		/* timeout case probably isn't too interesting */
		if (GET_ARG4(regfile)) {
			if (GET_SYSRET(new_regs) == 0) {
				/* timeout */
				make_sym(
					GET_ARG4(regfile),
					sizeof(struct timeval),
					"timeoutbuf");
				sc_ret_v_new(new_regs, 0);
				break;
			}
		}

		/* error */
		sc_ret_v_new(new_regs, (int)-1);
		break;
	}

	UNIMPL_SC(clone);

	case SYS_recvmsg: {
		struct msghdr	*mhdr;

		new_regs = sc_new_regs(regfile);

		if (GET_SYSRET(new_regs) == 0) {
			sc_ret_v(new_regs, 0);
			break;
		}

		if (GET_SYSRET_S(new_regs) == -1) {
			sc_ret_v(new_regs, -1);
			break;
		}

		mhdr = (void*)klee_get_value(GET_ARG1(regfile));
		klee_assume(mhdr->msg_iovlen >= 1);
		make_sym(
			(uint64_t)(mhdr->msg_iov[0].iov_base),
			mhdr->msg_iov[0].iov_len,
			"recvmsg_iov");
		mhdr->msg_controllen = 0;	/* XXX update? */

		klee_assume(GET_SYSRET(new_regs) == mhdr->msg_iov[0].iov_len);
		sc_ret_v(new_regs, mhdr->msg_iov[0].iov_len);
		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
	}
	break;

	case SYS_setsockopt:
		sc_ret_v(regfile, 0);
		break;
	case SYS_recvfrom:
		new_regs = sc_new_regs(regfile);
		make_sym(GET_ARG1(regfile), GET_ARG2(regfile), "recvfrom_buf");
		if (GET_ARG4(regfile))
			make_sym(
				GET_ARG4(regfile),
				sizeof(struct sockaddr_in),
				"recvfrom_sa");
		if (GET_ARG5(regfile) != 0) {
			socklen_t	*sl;
			sl = ((socklen_t*)GET_ARG5_PTR(regfile));
			*sl = sizeof(struct sockaddr_in);
		}
		sc_ret_or(new_regs, 0, GET_ARG2(regfile));
		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
		break;
	case SYS_sendto:
		sc_ret_or(sc_new_regs(regfile), -1, GET_ARG2(regfile));
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

	case SYS_timer_delete:
		klee_warning("phony timer_delete: always ret=success.");
		sc_ret_v(regfile, 0);
		break;

	case SYS_timer_settime:
		klee_warning("phony timer_settime");
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET_S(new_regs) == -1) {
			sc_ret_v(new_regs, -1);
			break;
		}
		
		sc_ret_v(new_regs, 0);
		break;

	case SYS_timer_create:
		klee_warning("phony timer_create");
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET_S(new_regs) == -1) {
			sc_ret_v(new_regs, -1);
			break;
		}
		
		sc_ret_v(new_regs, 0);
		break;

	case SYS_epoll_create:
		klee_warning_once("phony epoll_creat call");
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET_S(new_regs) == -1)
			break;
		klee_assume(GET_SYSRET(new_regs) > 3 && GET_SYSRET(new_regs) < 4096);
		break;

	case SYS_getsockopt:
		klee_warning_once("phony getsockopt");
		sc_ret_v(regfile, 0);
		break;

	case SYS_getsockname:
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET_S(new_regs) == -1)
			break;

		klee_assume(GET_SYSRET(new_regs) == 0);
		make_sym(GET_ARG1(regfile),
			sizeof(struct sockaddr_in),
			"getsockname");
		*((socklen_t*)GET_ARG2_PTR(regfile)) = sizeof(struct sockaddr_in);
		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
		break;

	case SYS_pipe2:
	case SYS_pipe:
		klee_warning_once("phony pipe");
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;
#if GUEST_ARCH_X86
	case X86_SYS_mmap2:
		GET_ARG5(regfile) *= 4096;
#endif
#if GUEST_ARCH_ARM
	case ARM_SYS_mmap2:
		GET_ARG5(regfile) *= 4096;
#endif
	case SYS_mmap:
		new_regs = sc_mmap(regfile);
		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
		sc_breadcrumb_commit(&sc, GET_SYSRET(new_regs));
		goto already_logged;
	case SYS_socket:
		klee_warning_once("phony socket call");
		sc_ret_range(sc_new_regs(regfile), -1, 4096);
		break;
	case SYS_fchdir:
	case SYS_chdir:
		klee_warning_once("phony chdir");
		sc_ret_v(regfile, 0);
		break;
	case SYS_klee:
		sc_klee(regfile);
		break;
	UNIMPL_SC(mremap)
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
		if (GET_SYSRET_S(new_regs) == -1)
			break;

		klee_assume(GET_SYSRET(new_regs) == 0);
		make_sym_by_arg(
			regfile,
			1,
			sizeof(struct ustat),
			"ustatbuf");
		break;

	case SYS_setresgid:
	case SYS_setresuid:
		sc_ret_v(regfile, 0);
		break;

	case SYS_getppid:
		klee_warning_once("Faking getppid syscall with 0x12345");
		/* XXX: */
		sc_ret_v(regfile, 0x12345);
		break;

	case SYS_fsync:
		new_regs = sc_new_regs(regfile);
		sc_ret_or(new_regs, 0, -1);
		break;
	case SYS_restart_syscall:
		new_regs = sc_new_regs(regfile);
		sc_ret_range(new_regs, -1, 1);
		break;

	case SYS_accept4:
		// int accept4(
		// 	int sockfd, struct sockaddr *addr,
		// 	socklen_t *addrlen, int flags);
		//
		// We'll be funny and mark 'addr's data as symbolic
		if (!GET_ARG1(regfile)) {
			sc_ret_v(regfile, fd_open_sym());
			break;
		}

		if (*(int*)GET_ARG2(regfile) < 4) {
			sc_ret_v(regfile, -1);
			break;
		}

		make_sym(GET_ARG1(regfile), 4, "accept4_addr");
		sc_ret_v(regfile, fd_open_sym());
		break;

	case SYS_sched_get_priority_min:
		sc_ret_v(regfile, 0);
		break;
	case SYS_sched_get_priority_max:
		sc_ret_v(regfile, 1);
		break;


	case SYS_chown:
	case SYS_shutdown:
	case SYS_inotify_init:
	case SYS_faccessat:
	case SYS_removexattr:
	case SYS_lremovexattr:
	case SYS_fremovexattr:
	case SYS_setxattr:
	case SYS_lsetxattr:
	case SYS_fsetxattr:
	case SYS_rmdir:
	case SYS_rename:
	case SYS_mknod:
	case SYS_unlinkat:
	case SYS_linkat:
	case SYS_sethostname:
	case SYS_setdomainname:
	case SYS_ftruncate:
	case SYS_chroot:
	case SYS_fchmodat:
	case SYS_fchownat:
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;

	case SYS_llistxattr:
	case SYS_flistxattr:
	case SYS_listxattr: {
		size_t len;

		if (GET_ARG1_PTR(regfile) == NULL) {
			sc_ret_v(regfile, -1);
			break;
		}

		len = concretize_u64(GET_ARG2(regfile));
		make_sym(GET_ARG1(regfile), len, "listxattr");
		sc_ret_v(regfile, len);
		break;
	}

	case SYS_syslog: {
		int	len;

		klee_warning_once("bogus syslog handling; whatever");
		if (	GET_ARG1_PTR(regfile) == NULL ||
			GET_ARG2_S(regfile) <= 0)
		{
			sc_ret_v(regfile, -1);
			break;
		}

		len = concretize_u64(GET_ARG2_S(regfile));
		make_sym(GET_ARG1(regfile), len, "syslog");
		sc_ret_v(regfile, len);
		break;
	}

	/* XXX may be broken for ARM */
	case SYS_statfs:
		if (GET_ARG1_PTR(regfile) == NULL) {
			sc_ret_v(regfile, -1);
			break;
		}

		new_regs = sc_new_regs(regfile);
		if ((int64_t)GET_SYSRET(new_regs) == -1) {
			sc_ret_v_new(new_regs, -1);
			break;
		}

		klee_assume(GET_SYSRET(new_regs) == 0);
		make_sym(GET_ARG1(regfile), sizeof(struct statfs), "statfs");

		sc_ret_v_new(new_regs, 0);
		break;

	case SYS_getxattr:
	case SYS_lgetxattr:
		if (GET_ARG2_PTR(regfile) == NULL) {
			sc_ret_v(regfile, -1);
			break;	
		}
		make_sym(GET_ARG2(regfile), GET_ARG3(regfile), "getxattr");
		sc_ret_or(sc_new_regs(regfile), -1, GET_ARG3(regfile));
		break;

	default:
		kmc_sc_bad(sc.sys_nr);
		klee_report_error(
			__FILE__,
			__LINE__,
			"Unknown Syscall",
			"sc.err");
		break;
	}

	if (sc_breadcrumb_is_newregs()) {
		/* ret value is stored in ktest regctx */
		sc_breadcrumb_commit(&sc, 0);
	} else {
		/* ret value was written concretely */
		sc_breadcrumb_commit(&sc, GET_SYSRET(regfile));
	}

already_logged:
	return jmpptr;
}


uint64_t concretize_u64(uint64_t s)
{
	uint64_t sc = klee_get_value(s);
	klee_assume(sc == s);
	return sc;
}

void* sc_new_regs(void* r)
{
	void	*ret = kmc_sc_regs(r);
	SC_BREADCRUMB_FL_OR(BC_FL_SC_NEWREGS);
	return ret;
}

void sc_ret_v(void* regfile, uint64_t v1)
{
	GET_SYSRET(regfile) = v1;
	klee_assume(GET_SYSRET_S(regfile) == (ARCH_SIGN_CAST)v1);
}

void sc_ret_v_new(void* regfile, uint64_t v1)
{
	klee_assume(GET_SYSRET_S(regfile) == (ARCH_SIGN_CAST)v1);
	GET_SYSRET(regfile) = v1;
}
