#define _LARGEFILE64_SOURCE

#include <sys/vfs.h>
#include <sys/sysinfo.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/uio.h>
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
#include "mem.h"
#include "syscalls.h"
#include "concrete_fd.h"
#include "breadcrumb.h"

//#define USE_SYS_FAILURE

static int last_sc = 0;

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

#define UNIMPL_SC(x)						\
	case SYS_##x:						\
		klee_uerror("Unimplemented syscall "#x, "sc.err");	\
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
	case KLEE_SYS_VALID_ADDR:
		sc_ret_v(regfile, klee_is_valid_addr(GET_ARG1_PTR(regfile)));
		break;
	default:
		klee_uerror("Unsupported SYS_klee syscall", "kleesc.err");
		break;
	}
}

static void loop_protect(int sc, int* ctr, int max_loop)
{
	*ctr = (last_sc == sc)
		? *ctr + 1
		: 0;

	if (*ctr < max_loop)
		return;

	klee_uerror("Possible syscall infinite loop", "loop.early");
}

#include <asm/ptrace.h>

void* sc_enter(void* regfile, void* jmpptr)
{
	struct sc_pkt		sc;
	void			*new_regs = NULL;

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
	case SYS_vfork:
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
	case SYS_write: {
		static int write_c = 0;
		loop_protect(SYS_write, &write_c, 1024);

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
	}
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

	case SYS_sync:
		break;
	case SYS_umask:
		sc_ret_v(regfile, 0666);
		break;

	case SYS_getpgid:
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

	case SYS_setfsuid:
	case SYS_setfsgid:
	case SYS_setpgid:
	case SYS_setsid:
	case SYS_setgid:
	case SYS_setuid:
	case SYS_setregid:
	case SYS_setreuid:
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;
	FAKE_SC_RANGE(geteuid, 0, 1)
	FAKE_SC_RANGE(getegid, 0, 1)
	FAKE_SC_RANGE(fcntl, -1, 1)
	FAKE_SC(fadvise64)
	FAKE_SC(rt_sigaction)

	case SYS_futex: {
		static int futex_c = 0;
		loop_protect(SYS_futex, &futex_c, 10);
		sc_ret_range(sc_new_regs(regfile), -1, 1);
		break;
	}

	case SYS_rt_sigsuspend: {
		/* XXX this should tweak a sighandler */
		static int suspend_c = 0;
		loop_protect(SYS_rt_sigsuspend, &suspend_c, 10);
		sc_ret_v(regfile, -1);
		break;
	}

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

	case SYS_pread64:
	case SYS_read:
	case SYS_fstat:
	case SYS_lstat:
	case SYS_creat:
	case SYS_readlink:
	case SYS_readlinkat:
	case SYS_lseek:
	case SYS_stat:
	case SYS_open:
	case SYS_openat:
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

		sc_ret_v(regfile, addr);
		sc_breadcrumb_commit(&sc, addr);
		goto already_logged;
	}
	break;

//	case SYS_waitpid: 32-bit only
	case SYS_wait4: {
		int *status;

		status = GET_ARG1_PTR(regfile);
		new_regs = sc_new_regs(regfile);
		if (status != NULL)
			make_sym((uint64_t)status, sizeof(int), "status");
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
#ifdef SYM_DENTS
#define MAX_DENT_SZ	512
	case SYS_getdents64:
	case SYS_getdents: {
		uint64_t	num_bytes = GET_ARG2(regfile);

		new_regs = sc_new_regs(regfile);
		if (num_bytes > MAX_DENT_SZ)
			num_bytes = MAX_DENT_SZ;
		sc_ret_or(new_regs, num_bytes, -1);
		if (GET_SYSRET_S(new_regs) == -1)
			break;
		make_sym_by_arg(regfile, 1, num_bytes, "getdents");
		break;
	}
#else
	case SYS_getdents64:
	case SYS_getdents: {
		new_regs = sc_new_regs(regfile);
		sc_ret_or(new_regs, 0, -1);
		break;
	}
	break;
#endif
	FAKE_SC(fchmod)
	FAKE_SC(fchown)
	FAKE_SC(utimensat)

	case SYS_clock_nanosleep: {
		if (GET_ARG3(regfile) != 0) {
			uint64_t dst_addr = concretize_u64(GET_ARG3(regfile));
			make_sym(
				dst_addr,
				sizeof(struct timespec),
				"clock_nanosleep");
			sc_ret_v(regfile, -1);
			break;
		}
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;
	}

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

	case SYS_time:
		if (GET_ARG0(regfile) == 0) {
			/* only need to return symbolic ret reg */
			new_regs = sc_new_regs(regfile);
			break;
		}

		/* otherwise, mark buffer as symbolic */
		make_sym(GET_ARG0(regfile), 4, "time");
		sc_ret_v(regfile, GET_ARG0(regfile));
		break;

	case SYS_utime:
		sc_ret_or(sc_new_regs(regfile), 0, -1);
		break;

	case SYS_madvise:
		sc_ret_v(regfile, 0);
		break;

	case SYS_pselect6:
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

			if (sc.sys_nr != SYS_pselect6 && GET_ARG4(regfile)) {
				make_sym(
					GET_ARG4(regfile),
					sizeof(struct timeval),
					"timeoutbuf");
			}

			sc_ret_v_new(new_regs, (int)GET_ARG0(regfile));
			break;
		}

		/* timeout case probably isn't too interesting */
		if (sc.sys_nr != SYS_pselect6 && GET_ARG4(regfile)) {
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

	case ARCH_SYS_DEFAULT_EQ0:
		sc_ret_v(regfile, 0);
		break;

	case ARCH_SYS_DEFAULT_LE0:
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;


	case SYS_brk: {
		new_regs = sc_brk(regfile);
		break;
	}
	case SYS_munmap:
		sc_munmap(regfile);
		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
		break;

	case ARCH_SYS_MMAP2:
		/* XXX: NOTE: mmap2 means pgoff is in pages, not bytes. OOPS */
	case SYS_mmap: {
		uint64_t len = GET_ARG1(regfile);
		if (sc.sys_nr == ARCH_SYS_MMAP2) {
			// klee_print_expr("DONT BOOST MMAP2", len);
			// len *= 4096;
		}

		if (GET_ARG1(regfile) == 0) {
			sc_ret_v(regfile, ~0);
			break;
		}
		if (new_regs == NULL)
			new_regs = sc_mmap(regfile, len);

		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
		sc_breadcrumb_commit(&sc, GET_SYSRET(new_regs));
		goto already_logged;
	}

	case SYS_mremap:
		/* bad address / not page aligned */
		if (	GET_ARG1(regfile) == 0 ||
			((GET_ARG1(regfile)) & 0xfff) != 0)
		{
			sc_ret_v(regfile, ~0);
			break;
		}

		new_regs = sc_mremap(regfile);
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

	case SYS_restart_syscall:
		new_regs = sc_new_regs(regfile);
		sc_ret_range(new_regs, -1, 1);
		break;

	case SYS_accept:
		/* int accept(
		 * 	int sockfd, struct sockaddr *addr,
		 * 	socklen_t *addrlen); */

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

	case SYS_symlink:
	case SYS_clock_settime:
#ifndef SYS_clock_adjtime
#define SYS_clock_adjtime 305
#endif
	case SYS_clock_adjtime:
	case SYS_chown:
	case SYS_shutdown:
	case SYS_inotify_init:
	case SYS_inotify_init1:
	case SYS_inotify_add_watch: /* XXX this should be some kind of desc */
	case SYS_inotify_rm_watch:
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
	case SYS_link:
	case SYS_linkat:
	case SYS_unlink:
	case SYS_unlinkat:
	case SYS_sethostname:
	case SYS_setdomainname:
	case SYS_ftruncate:
	case SYS_chroot:
	case SYS_fchmodat:
	case SYS_fchownat:
	case SYS_fdatasync:
	case SYS_fsync:
	case SYS_mount:
	case SYS_umount2:
	case SYS_capset:
	case SYS_mlock:
	case SYS_mlockall:
	case SYS_munlock:
	case SYS_munlockall:
	case SYS_flock:
	case SYS_sched_setscheduler:
	case SYS_setgroups:
	case SYS_iopl:
	case SYS_socketpair:
	case SYS_setpriority:
	case SYS_setitimer:
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;

	case SYS_getpriority:
	case SYS_semget:
		new_regs = sc_new_regs(regfile);
		break;


	case SYS_pause: sc_ret_v(regfile, -1); break;
	case SYS_getitimer:
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET_S(new_regs) == -1)
			break;

		if (GET_ARG1_PTR(regfile) != NULL) {
			make_sym(
				(uint64_t)GET_ARG1_PTR(regfile),
				sizeof(struct itimerval),
				"itimer");
		}

		sc_ret_v_new(new_regs, 0);
		break;


	/* sys_clone is actually much simpler than clone()! */
// from the kernel:
// asmlinkage int sys_clone(unsigned long clone_flags, unsigned long newsp,
// unsigned long parent_tidp, unsigned long child_tidp)

	case SYS_clone:
		new_regs = sc_new_regs(regfile);
		if (GET_ARG1_PTR(regfile) == NULL)
			break;

		klee_warning("Is clone()'s stack switching right?");
		klee_assume(GET_STACK(new_regs) == GET_ARG1(regfile));
		break;

	case SYS_pwrite64:
		sc_ret_or(sc_new_regs(regfile), -1, GET_ARG2(regfile));
		break;
	case SYS_readv: {
		struct iovec* iov;
		int total_bytes = 0;
		unsigned i;

		iov = GET_ARG1_PTR(regfile);
		if (iov == NULL) {
			sc_ret_v(regfile, -1);
			break;
		}

		for (i = 0; i < GET_ARG2(regfile); i++) {
			total_bytes += iov[i].iov_len;
			if (iov->iov_len && iov[i].iov_base != NULL)
				make_sym(
					(uint64_t)iov[i].iov_base,
					iov[i].iov_len,
					"readv");
		}


		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
		break;
	}

	case SYS_sendmsg: {
		const struct msghdr	*mh;
		int			total_bytes = 0;
		unsigned 		i;

		if (!GET_ARG1(regfile)) {
			sc_ret_v(regfile, -1);
			break;
		}

		mh = GET_ARG1_PTR(regfile);
		for (i = 0; i < mh->msg_iovlen; i++)
			total_bytes += mh->msg_iov[i].iov_len;

		sc_ret_or(sc_new_regs(regfile), -1, total_bytes);
		break;
	}

	case SYS_execve: {
		int 	i;
		char	**argv;

		new_regs = sc_new_regs(regfile);
		sc_ret_or(new_regs, -1, 0);
		if (GET_SYSRET_S(new_regs) == -1) {
			/* execve can fail sometimes */
			break;
		}

		/* check strings for symbolic / tainted data */
		if (	GET_ARG0_PTR(regfile) && 
			file_path_has_sym(GET_ARG0_PTR(regfile)))
		{
			klee_uerror("Symbolic exec filepath", "symexec.err");
		}

		argv = GET_ARG1_PTR(regfile);
		for (i = 0; argv[i] != NULL; i++) {
			if (argv[i] && file_path_has_sym(argv[i])) {
				klee_uerror("Symbolic exec argv", "symexec.err");
			}
		}

		kmc_exit(0);
		break;
	}

	case SYS_getresgid:
	case SYS_getresuid:
		if (!GET_ARG0(regfile)
			|| !GET_ARG1(regfile)
			|| !GET_ARG2(regfile))
		{
			sc_ret_v(regfile, -1);
			break;
		}
		make_sym(GET_ARG0(regfile), sizeof(uid_t), "res_id");
		make_sym(GET_ARG1(regfile), sizeof(uid_t), "res_eid");
		make_sym(GET_ARG2(regfile), sizeof(uid_t), "res_sid");
		sc_ret_v(regfile, 0);
		break;

	case SYS_eventfd: {
		sc_ret_or(sc_new_regs(regfile),
			-1, /* error */
			0x7e00 /* fake eventfd */);
		break;
	}

	case SYS_capget:
		if (!GET_ARG1(regfile)) {
			/* no pointer given */
			sc_ret_v(regfile, -1);
			break;
		}

		new_regs = sc_new_regs(regfile);
		sc_ret_or(new_regs, 0, -1);
		if (GET_SYSRET(new_regs) != 0)
			break;

		make_sym(GET_ARG1(regfile), 4, "capget");
		break;

	case SYS_reboot:
		new_regs = sc_new_regs(regfile);
		sc_ret_or(new_regs, -1, 0);
		if (GET_SYSRET(new_regs) == 0)
			kmc_exit(0);
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
	case SYS_fstatfs:
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

	case SYS_gettimeofday:
		if (GET_ARG0_PTR(regfile) == NULL) {
			sc_ret_v(regfile, -1);
			break;
		}

		new_regs = sc_new_regs(regfile);
#if defined(GUEST_ARCH_X86) || defined(GUEST_ARCH_ARM)
#define TIMEVAL_BYTES	8
#else
#define TIMEVAL_BYTES	sizeof(struct timeval)
#endif
		make_sym(GET_ARG0(regfile), TIMEVAL_BYTES, "timeofday");
		sc_ret_or(new_regs, 0, -1);
		break;

	case ARCH_SYS_UNSUPP:
	default:
		kmc_sc_bad(sc.sys_nr);
		klee_uerror("Unknown Syscall", "sc.err");
		break;
	}

	last_sc = sc.sys_nr;

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

void* concretize_ptr(void* s)
{
	uint64_t s2 = klee_get_value((uint64_t)s);
	klee_assume_eq((uint64_t)s, s2);
	return (void*)s2;
}

uint64_t concretize_u64(uint64_t s)
{
	uint64_t sc = klee_get_value(s);
	klee_assume_eq(sc, s);
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
//	klee_assume_eq(GET_SYSRET_S(regfile), (ARCH_SIGN_CAST)v1);
	GET_SYSRET(regfile) = v1;
}

void sc_ret_v_new(void* regfile, uint64_t v1)
{
	klee_assume_eq(GET_SYSRET_S(regfile), (ARCH_SIGN_CAST)v1);
	GET_SYSRET(regfile) = v1;
}
