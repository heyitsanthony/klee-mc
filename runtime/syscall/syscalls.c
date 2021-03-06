#define _LARGEFILE64_SOURCE
#include <string.h>
#include <stdbool.h>
#include <sys/ptrace.h>
#include <sys/user.h>
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
#include <sys/utsname.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <ustat.h>
#include <sys/syscall.h>
#include <klee/klee.h>
#include <grp.h>
#include <asm/ldt.h>
#include <asm/ptrace.h>
#include <string.h>
#include "cpu/i386_macros.h"

#include "struct_sz.h"
#include "file.h"
#include "mem.h"
#include "syscalls.h"
#include "concrete_fd.h"
#include "breadcrumb.h"

#define SYM_DENTS
//#define USE_SYS_FAILURE
extern bool concrete_vfs;
static int last_sc = 0;
static unsigned sc_c = 0;
static stack_t cur_sigstack = {
	.ss_sp = (void*)0xdeadbeef,
	.ss_flags = 0,
	.ss_size = 0
};

extern void proc_sc(struct sc_pkt* sc);

static struct kreport_ent badbuf_ktab[2] = 
{	MK_KREPORT("address"),
	MK_KREPORT(NULL)
};

void sc_report_badbuf(const char* v, const void* p)
{
	SET_KREPORT(&badbuf_ktab[0], p);
	klee_ureport_details(v, "scbuf.err", &badbuf_ktab);
}


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

static void sc_ret_ge(void* regfile, int64_t v)
{
	ARCH_SIGN_CAST rax = GET_SYSRET_S(regfile);
	klee_assume_sge(rax, v);
}

#define sc_ret_ge0(x)	sc_ret_ge(x, 0)

void sc_ret_or(void* regfile, ARCH_CAST v1, ARCH_CAST v2)
{
	ARCH_SIGN_CAST rax = GET_SYSRET(regfile);
	int	is_v1, is_v2;

	is_v1 = rax == (ARCH_SIGN_CAST)v1;
	is_v2 = rax == (ARCH_SIGN_CAST)v2;
	klee_assume_ne(is_v1 | is_v2, 0);
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
	klee_assume_sge(rax, (ARCH_SIGN_CAST)lo);
	klee_assume_sle(rax, (ARCH_SIGN_CAST)hi);
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

#define UNIMPL_SC(x)						\
	case SYS_##x:						\
		klee_ureport("Unimplemented syscall "#x, "sc.err");	\
		sc_ret_v(regfile, -1);				\
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

#define FAKE_SC_OR(x,y,z)					\
	case SYS_##x:						\
		klee_warning_once("Faking OR syscall "#x);	\
		new_regs = sc_new_regs(regfile);		\
		if (GET_SYSRET(new_regs) == y)			\
			break;					\
		sc_ret_v_new(new_regs, z);			\
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

/* for SYS_* definitions */
#include <linux/net.h>

static void do_sockcall(void* regfile, int call, unsigned long* args)
{
	void	*new_regs;

	klee_print_expr("do_sockcall", call);

	switch (call) {
	case SYS_SOCKET: {
		static int sockfd = 3000;
		new_regs = sc_new_regs(regfile);
		if (klee_prefer_ne(GET_SYSRET_S(new_regs), -1))
			sc_ret_v_new(new_regs, sockfd++);
		else
			sc_ret_v_new(new_regs, - 1);
		break;
	}
	case SYS_BIND: sc_ret_or(sc_new_regs(regfile), 0, -1); break;
	case SYS_CONNECT: sc_ret_or(sc_new_regs(regfile), -1, 0); break;
	case SYS_LISTEN: sc_ret_v(regfile, 0); break;

	case SYS_GETSOCKNAME:
		if (	!klee_is_symbolic(args[1]) && 
			!klee_is_valid_addr((void*)args[1]))
		{
			sc_ret_v(regfile, -1);
			break;
		}

		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET_S(new_regs) == -1)
			break;

		sc_ret_v_new(new_regs, 0);
		make_sym(args[1], sizeof(struct sockaddr_in), "getsockname");
		*((socklen_t*)args[2]) = sizeof(struct sockaddr_in);
		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
		break;

	case SYS_GETPEERNAME:
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET_S(new_regs) == -1)
			break;

		sc_ret_v_new(new_regs, 0);
		make_sym_by_arg(
			regfile,
			1,
			klee_get_value(*((socklen_t*)args[2])),
			"getpeeraddr");
		break;

	case SYS_SEND:
	case SYS_RECV:
	case SYS_SENDTO:
		sc_ret_or(sc_new_regs(regfile), -1, args[2]);
		break;

	case SYS_RECVFROM: {
		uint64_t	len;

		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET_S(new_regs) == -1)
			break;

		len = args[2];
		if (args[2] > 8192) len = 8192;

		/* argument buffer */
		make_sym(args[1], len, "recvfrom_buf");

		/* sockaddr pointer? */
		if (args[4])
			make_sym(
				args[4],
				sizeof(struct sockaddr_in),
				"recvfrom_sa");

		/* socklen set? */
		if (args[5]) {
			socklen_t	*sl;
			sl = ((socklen_t*)args[5]);
			*sl = sizeof(struct sockaddr_in);
		}

		sc_ret_v_new(new_regs, len);
		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
		break;
	}

	case SYS_SOCKETPAIR:
	case SYS_SHUTDOWN:
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;

	case SYS_SETSOCKOPT:
		sc_ret_v(regfile, 0);
		break;

	case SYS_GETSOCKOPT:
		klee_warning_once("phony getsockopt");
		sc_ret_v(regfile, 0);
		break;

	case SYS_SENDMSG: {
		const struct msghdr	*mh;
		int			total_bytes = 0;
		unsigned 		i;

		if (!args[1]) {
			sc_ret_v(regfile, -1);
			break;
		}

		mh = (void*)args[1];
		for (i = 0; i < mh->msg_iovlen; i++)
			total_bytes += mh->msg_iov[i].iov_len;

		sc_ret_or(sc_new_regs(regfile), -1, total_bytes);
		break;
	}


	case SYS_RECVMSG:
	{
		struct msghdr	*mhdr;

		new_regs = sc_new_regs(regfile);

		mhdr = (void*)concretize_u64(args[1]);
		if (mhdr != NULL) {
			make_sym(
				(uint64_t)&mhdr->msg_flags,
				sizeof(mhdr->msg_flags),
				"recvmsg_flags");

			if (mhdr->msg_control != NULL) {
				make_sym(
					(uint64_t)(mhdr->msg_control),
					mhdr->msg_controllen,
					"recvmsg_ctl");
			}
		}

		if (GET_SYSRET(new_regs) == 0) {
			klee_print_expr("rm", 0);
			sc_ret_v_new(new_regs, 0);
			break;
		}

		if (mhdr == NULL) {
			sc_ret_v_new(new_regs, -1);
			break;

		}

		if (mhdr->msg_controllen != 0) {
			klee_print_expr(
				"control messages not yet supported!",
				mhdr->msg_controllen);
		}

		if (mhdr->msg_iovlen == 0) {
			sc_ret_v_new(new_regs, -1);
			break;
		}


		if (GET_SYSRET_S(new_regs) == -1) {
			klee_print_expr("rm", -1);
			sc_ret_v_new(new_regs, -1);
			break;
		}

		make_sym(
			(uint64_t)(mhdr->msg_iov[0].iov_base),
			mhdr->msg_iov[0].iov_len,
			"recvmsg_iov");

		if (mhdr->msg_name != NULL) {
			make_sym(
				(uint64_t)(mhdr->msg_name),
				mhdr->msg_namelen,
				"recvmsg_name");
		}

		/* TODO: controllen should be symbolic */
		sc_ret_v_new(new_regs, mhdr->msg_iov[0].iov_len);
		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
		break;
	}


	case SYS_ACCEPT:
	case SYS_ACCEPT4:
		// int accept4(
		// 	int sockfd, struct sockaddr *addr,
		// 	socklen_t *addrlen, int flags);
		//
		// We'll be funny and mark 'addr's data as symbolic
		if (!args[1]) {
			sc_ret_v(regfile, fd_open_sym());
			break;
		}

		/* addrlen */
		if (*((int*)args[2]) < 4) {
			sc_ret_v(regfile, -1);
			break;
		}

		make_sym(args[1], 4, "accept4_addr");
		sc_ret_v(regfile, fd_open_sym());
		break;

	case SYS_RECVMMSG:
	case SYS_SENDMMSG:
		kmc_sc_bad(call);
		klee_uerror("Unknown SocketCall", "sc.err");
		break;
	}
}

static void sc_poll(void* regfile)
{
	struct pollfd	*fds;
	uint64_t	poll_addr;
	static int	poll_c = 0;
	unsigned int	i, nfds;
	void		*new_regs = sc_new_regs(regfile);

	loop_protect(SYS_poll, &poll_c, 16);

	if (GET_SYSRET_S(new_regs) == -1) {
		sc_ret_v_new(new_regs, -1);
		return;
	}

	if (GET_SYSRET_S(new_regs) == 0 || GET_ARG1(regfile) == 0) {
		sc_ret_v_new(new_regs, 0);
		return;
	}

	nfds = concretize_u64(GET_ARG1(regfile));

	poll_addr = concretize_u64(GET_ARG0(regfile));
	fds = (struct pollfd*)poll_addr;

	/* XXX: this is killing performance in php */
	for (i = 0; i < nfds; i++) {
	//	klee_check_memory_access(&fds[i], sizeof(struct pollfd));
		klee_check_memory_access(&fds[i], 1);
	//	fds[i].revents = klee_get_value(fds[i].events);
		make_sym((uint64_t)&fds[i].revents, sizeof(short), "revents");
	//	fds[i].revents = fds[i].events;
	}

	sc_ret_v_new(new_regs, nfds);
}

static void sc_klee(void* regfile)
{
	unsigned int	sys_klee_nr;

	sys_klee_nr = GET_ARG0(regfile);
	switch(sys_klee_nr) {
	case KLEE_SYS_INDIRECT0:
		sc_ret_v(regfile, klee_indirect0(GET_ARG1_PTR(regfile)));
		break;
	case KLEE_SYS_INDIRECT1:
		sc_ret_v(regfile,
			klee_indirect1(
				GET_ARG1_PTR(regfile),
				GET_ARG2(regfile)));
		break;
	case KLEE_SYS_INDIRECT2:
		sc_ret_v(regfile,
			klee_indirect2(
				GET_ARG1_PTR(regfile),
				GET_ARG2(regfile),
				GET_ARG3(regfile)));
		break;
	case KLEE_SYS_INDIRECT3:
		sc_ret_v(regfile,
			klee_indirect3(
				GET_ARG1_PTR(regfile),
				GET_ARG2(regfile),
				GET_ARG3(regfile),
				GET_ARG4(regfile)));
		break;

	case KLEE_SYS_INDIRECT4:
		sc_ret_v(regfile,
			klee_indirect4(
				GET_ARG1_PTR(regfile),
				GET_ARG2(regfile),
				GET_ARG3(regfile),
				GET_ARG4(regfile),
				GET_ARG5(regfile)));
		break;


	case KLEE_SYS_REPORT_ERROR:
		klee_report_error(
			(const char*)GET_ARG1_PTR(regfile),
			GET_ARG2(regfile),
			(const char*)klee_get_value(GET_ARG3(regfile)),
			(const char*)klee_get_value(GET_ARG4(regfile)),
			NULL);
		break;
	case KLEE_SYS_KMC_SYMRANGE:
		make_sym(
			GET_ARG1(regfile),	/* addr */
			GET_ARG2(regfile),	/* len */
			(const char*)GET_ARG3_PTR(regfile) /* name */);
		sc_ret_v(regfile, 0);
		break;
	case KLEE_SYS_IS_SYM:
		sc_ret_v(regfile, klee_is_symbolic(GET_ARG1(regfile)));
		break;
	case KLEE_SYS_NE:
		klee_assume_ne(GET_ARG1(regfile), GET_ARG2(regfile));
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
	case KLEE_SYS_IS_SHADOWED:
		sc_ret_v(regfile, klee_is_shadowed(GET_ARG1(regfile)));
		break;
	default:
		klee_uerror("Unsupported SYS_klee syscall", "kleesc.err");
		break;
	}
}

unsigned sc_count(void) { return sc_c; }

void* sc_enter(void* regfile, void* jmpptr)
{
	struct sc_pkt		sc;
	void			*new_regs = NULL;

	sc_c++;

	sc_clear(&sc);
	sc.regfile = regfile;
	sc.pure_sys_nr = GET_SYSNR(regfile);

	/* XXX: update breadcrumbs? */
	if (kmc_ossfx_load()) return jmpptr;

	if (klee_is_symbolic(sc.pure_sys_nr)) {
		klee_warning_once("Resolving symbolic syscall nr");
		sc.pure_sys_nr = klee_fork_all(sc.pure_sys_nr);
	}

	sc.sys_nr = sc.pure_sys_nr;

#ifndef GUEST_ARCH_AMD64
	/* non-native architecture */
	syscall_xlate(&sc);
#endif

	sc_breadcrumb_reset();

	switch (sc.sys_nr) {
	case SYS_mprotect:
		klee_warning_once("faking OK mprotect()");
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
			sc_ret_v_new(new_regs, GET_ARG2(regfile));
		} else
#endif
			sc_ret_v(regfile, GET_ARG2(regfile));
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

	case SYS_ptrace: {
		int pt_call = GET_ARG0(regfile);

		if (pt_call == PTRACE_GETREGS) {
			make_sym_by_arg(
				regfile,
				3,
				PTRACE_REGS_SZ,
				"user_regs_struct");
			sc_ret_v(regfile, 0);
			break;
		} else if (pt_call == PTRACE_GETFPREGS) {
			make_sym_by_arg(
				regfile,
				3,
				PTRACE_FPREGS_SZ,
				"user_regs_struct");
			sc_ret_v(regfile, 0);
			break;
		}

		new_regs = sc_new_regs(regfile);
		break;
	}

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
		sc_ret_or(sc_new_regs(regfile), 0, -1);
		make_sym_by_arg(regfile, 1, TIMESPEC_SZ, "clock_getres");
		break;

	case SYS_clock_gettime: {
		void*	timespec = GET_ARG1_PTR(regfile);
		if (timespec == NULL) {
			sc_ret_v(regfile, -1);
			break;
		}
		make_sym_by_arg(regfile, 1, TIMESPEC_SZ, "timespec");
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
	FAKE_SC_OR(geteuid, 0, 1)
	FAKE_SC_OR(getegid, 0, 1)
	case SYS_fcntl: {
		int	fd = GET_ARG0(regfile);
		int	cmd = GET_ARG1(regfile);

		if (!concrete_vfs) {
			sc_ret_range(sc_new_regs(regfile), -1, 1);
			break;
		}

		if (fd_is_concrete(fd) || fd == 3) {
			if (cmd == F_SETFD) {
				sc_ret_v(regfile, 0);
				break;
			} else if (cmd == F_DUPFD) {
				sc_ret_v(regfile, fd_dup(fd, GET_ARG2(regfile)));
				break;
			}
		}

		klee_warning_once("Faking range syscall fcntl");
		klee_print_expr("fnctl cmd", cmd);
		klee_print_expr("fnctl fd", fd);
		sc_ret_range(sc_new_regs(regfile), -1, 1);
		break;
	}
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

	case SYS_rt_sigtimedwait: {
		/* XXX this should tweak a sighandler */
		static int suspend_c = 0;
		loop_protect(SYS_rt_sigtimedwait, &suspend_c, 10);
		sc_ret_v(regfile, 0);
		break;
	}


	case SYS_rt_sigprocmask:
		if (GET_ARG2(regfile)) {
			make_sym_by_arg(regfile, 2, SIGSET_T_SZ, "sigset");
			sc_ret_v(regfile, 0);
		} else {
//			sc_ret_or(sc_new_regs(regfile), -1, 0);
			sc_ret_v(regfile, 0);
		}
		break;

	case SYS_kill:
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;

	case SYS_keyctl:
		new_regs = sc_new_regs(regfile);
		break;

	case SYS_sysinfo:
		make_sym_by_arg(regfile, 0, SYSINFO_SZ, "sysinfo");
		sc_ret_v(regfile, 0);
		break;

	case SYS_getgroups: {
		unsigned	sz;
		if (GET_ARG0_S(regfile) < 0) {
			sc_ret_v(regfile, -1);
			break;
		}

		if (GET_ARG0(regfile) == 0) {
			sc_ret_v(regfile, 0);
			break;
		}

		sz = GET_ARG0(regfile);
		if (sz > 16) sz = 16;

		make_sym_by_arg(
			regfile, 1,
			sizeof(gid_t)*sz,
			"getgroups");
		sc_ret_v(regfile, sz);
		break;
	}
	case SYS_sched_setaffinity:
	case SYS_sched_getaffinity:
		sc_ret_v(regfile, -1);
		break;

	case SYS_newfstatat: { /* for du */
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET_S(new_regs) == -1) {
			break;
		}
		klee_assume_eq(GET_SYSRET(new_regs), 0);
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
	case SYS_access:
		if (!file_sc(&sc)) goto skip_commit;
		break;

#ifdef GUEST_ARCH_AMD64
	case SYS_arch_prctl:
		((VexGuestAMD64State*)regfile)->guest_FS_CONST = GET_ARG1(regfile);
		sc_ret_v(regfile, 0);
		break;
#endif

	case SYS_prctl:
		sc_ret_v(regfile, -1);
		break;
	case SYS_ioctl:
		if (fd_is_concrete(GET_ARG0(regfile))) {
			klee_print_expr(
				"[warning] failing ioctl on concrete fd",
				GET_ARG0(regfile));
			sc_ret_v(regfile, -1);
			break;
		}
		new_regs = sc_new_regs(regfile);
		klee_print_expr("ioctl fd", GET_ARG0(regfile));
		klee_print_expr("ioctl arg1", GET_ARG1(regfile));
		klee_print_expr("ioctl arg2", GET_ARG2(regfile));
		sc_ret_ge(new_regs, -1);
		break;

	case SYS_uname: {
		klee_warning_once("Using bogus uname");
		struct utsname*	buf = GET_ARG0_PTR(regfile);
		memcpy(&buf->sysname, "Linux", 6);
		memcpy(&buf->nodename, "kleemc", 7);
		memcpy(&buf->release, "3.9.6", 6);
		memcpy(&buf->version, "x", 2);
		memcpy(&buf->machine, "x86_64", 7);/* XXX */
		sc_ret_v(regfile, 0);
		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
		break;
	}

	case SYS_writev:
		sc_ret_ge0(sc_new_regs(regfile));
		break;

	case SYS_sigaltstack:
		klee_warning_once("assuming sigaltstack succeeds");
		const stack_t	*ss = GET_ARG0_PTR(regfile);
		stack_t		*oss = GET_ARG1_PTR(regfile);
		if (oss) memcpy(oss, &cur_sigstack, sizeof(stack_t));
		memcpy(&cur_sigstack, ss, sizeof(stack_t));
		sc_ret_v(regfile, 0);
		break;

	case SYS_getcwd: {
		uint64_t addr = GET_ARG0(regfile);
		uint64_t len = GET_ARG1(regfile);

		if (len == 0 || addr == 0) {
			sc_ret_v(regfile, 0);
			break;
		}

		addr = concretize_u64(GET_ARG0(regfile));

		if (concrete_vfs) {
			int	n = (len < 6) ? len : 6;
			klee_warning_once("Fake cwd=/fake for concrete VFS.");
			memcpy((void*)addr, "/fake", n);
			sc_ret_v(regfile, n);
			break;
		}

		/* use managably-sized string */
		if (len > 10) len = 10;

		make_sym_by_arg(regfile, 0, len, "cwdbuf");

		// XXX remember to do this on the other side!!
		((char*)addr)[len-1] = '\0';

		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);

		sc_ret_v(regfile, addr);
		sc_breadcrumb_commit(&sc, addr);
		goto skip_commit;
	}
	break;

	case SYS_waitid: {
		siginfo_t	*infop;

		infop = GET_ARG2_PTR(regfile);
		new_regs = sc_new_regs(regfile);
		if (infop != NULL)
			make_sym_by_arg(regfile, 2, SIGINFO_T_SZ, "waitid_siginfo");
		break;
	}

	case ARCH_SYS_WAITPID: // 32-bit only
	case SYS_wait4: {
		int *status;

		status = GET_ARG1_PTR(regfile);
		new_regs = sc_new_regs(regfile);
		if (status != NULL)
			make_sym_by_arg(regfile, 1, sizeof(int), "status");
		break;
	}

	case SYS_set_thread_area: {
#ifdef GUEST_ARCH_X86
		struct user_desc	*ud = GET_ARG0_PTR(regfile);
		VexGuestX86SegDescr	*vsd;
		VexGuestX86State	*vs;
		int			entry_num;

		entry_num = ud->entry_number;
		klee_print_expr(
			"setting thread area for entry number",
			entry_num);

		vs = ((VexGuestX86State*)regfile);
		vsd = (VexGuestX86SegDescr*)((void*)((vs->guest_GDT)));
		
		if (entry_num == -1) {
			/* find entry num */
			int	i;
			for (i = 1; i < 1024; i++) {
				if (vsd[i].LdtEnt.Bits.Pres == 0)
					break;
			}
			entry_num = i;
			klee_print_expr(
				"alloc thread area entry number",
				entry_num);
			ud->entry_number = entry_num;
		}

		ud2vexseg(*ud, &vsd[entry_num].LdtEnt);
		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
		sc_ret_v(regfile, 0);
#else
		/* fail on amd64 */
		sc_ret_v(regfile, -1);
#endif
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
		sc_ret_v(regfile, fd_dup(GET_ARG0(regfile), 0));
		break;
	case SYS_dup2:
		sc_ret_v(regfile, fd_dup2(GET_ARG0(regfile), GET_ARG1(regfile)));
		break;
	case SYS_dup3: {
		int oldfd = GET_ARG0(regfile), newfd = GET_ARG1(regfile);
		if (newfd == oldfd) {
			/* EINVAL */
			sc_ret_v(regfile, -1);
			break;
		}
		sc_ret_v(regfile, fd_dup2(oldfd, newfd));
		break;
	}
	case SYS_setrlimit:
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;
	case SYS_getrlimit:
		make_sym_by_arg(regfile, 1, RLIMIT_SZ, "getrlimit");
		sc_ret_v(regfile, 0);
		break;
	case SYS_getrusage:
		sc_ret_v(regfile, 0);
		make_sym_by_arg(regfile, 1, RUSAGE_SZ, "getrusage");
		break;
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
	FAKE_SC(fchmod)
	FAKE_SC(fchown)
	FAKE_SC(lchown)
	FAKE_SC(utimensat)

	case SYS_clock_nanosleep: {
		if (GET_ARG3(regfile) != 0) {
			// uint64_t dst_addr = concretize_u64(GET_ARG3(regfile));
			make_sym_by_arg(
				regfile, 3, TIMESPEC_SZ,  "clock_nanosleep");
		}
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;
	}

	case SYS_nanosleep: {
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET_S(new_regs) == -1) {
			/* terminated early */
			if (GET_ARG1(regfile) != 0) {
				make_sym_by_arg(
					regfile, 1, TIMESPEC_SZ, "nanosleep");
			}
			sc_ret_v_new(new_regs, -1);
		} else {
			/* terminated on time */
			sc_ret_v_new(new_regs, 0);
		}
		break;
	}

	case SYS_time:
		if (GET_ARG0(regfile) == 0) {
			/* only need to return symbolic ret reg */
			new_regs = sc_new_regs(regfile);
			break;
		}

		/* otherwise, mark buffer as symbolic */
		make_sym_by_arg(regfile, 0, 4, "time");
		sc_ret_v(regfile, GET_ARG0(regfile));
		break;

	case SYS_utime:
		sc_ret_or(sc_new_regs(regfile), 0, -1);
		break;

	case SYS_mbind:
	case SYS_madvise:
		sc_ret_v(regfile, 0);
		break;

	case SYS_pselect6:
	case SYS_select: {
		new_regs = sc_new_regs(regfile);

		/* let all through */
		if (GET_SYSRET(new_regs) == GET_ARG0(regfile)) {
			if (GET_ARG1(regfile))
				make_sym_by_arg(
					regfile,
					1,
					sizeof(fd_set), "readfds");
			if (GET_ARG2(regfile))
				make_sym_by_arg(
					regfile,
					2,
					sizeof(fd_set), "writefds");
			if (GET_ARG3(regfile))
				make_sym_by_arg(
					regfile,
					3,
					sizeof(fd_set), "exceptfds");

			if (sc.sys_nr != SYS_pselect6 && GET_ARG4(regfile)) {
				make_sym_by_arg(
					regfile, 4, TIMEVAL_SZ,	"timeoutbuf");
			}

			sc_ret_v_new(new_regs, (int)GET_ARG0(regfile));
			break;
		}

		/* timeout case probably isn't too interesting */
		if (	sc.sys_nr != SYS_pselect6
			&& GET_ARG4(regfile)
			&& GET_SYSRET(new_regs) == 0)
		{
			make_sym_by_arg(regfile, 4, TIMEVAL_SZ, "timeoutbuf");
			sc_ret_v_new(new_regs, 0);
			break;
		}

		/* error */
		sc_ret_v_new(new_regs, (int)-1);
		break;
	}


	case SYS_chmod:
		klee_warning_once("phony chmod");
		sc_ret_v(regfile, 0);
		break;
	case SYS_mkdir:
		klee_warning_once("phony mkdir");
		sc_ret_v(regfile, 0);
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

	case SYS_epoll_create1:
	case SYS_epoll_create:
		klee_warning_once("phony epoll_creat call");
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET_S(new_regs) == -1)
			break;
		klee_assume_ugt(GET_SYSRET(new_regs), 3);
		klee_assume_ult(GET_SYSRET(new_regs), 4096);
		break;

	case SYS_epoll_ctl:
		sc_ret_v(regfile, 0);
		break;

	case SYS_epoll_wait:
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET_S(new_regs) == -1)
			break;

		klee_assume_uge(GET_SYSRET(new_regs), 0);
		klee_assume_ule(GET_SYSRET(new_regs), GET_ARG2(regfile));
		make_sym_by_arg(
			regfile,
			1,
			EPOLL_EVENT_SZ * GET_ARG2(regfile),
			"epoll_wait");
		break;

	case SYS_pipe2:
	case SYS_pipe:
		klee_warning_once("phony pipe");
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;

	case ARCH_SYS_SOCKETCALL: {
		/* XXX assuming 32-bit arch... */
		uint32_t	*v32 = GET_ARG1_PTR(regfile);
		unsigned long v[] = { v32[0], v32[1], v32[2], v32[3], v32[4], v32[5] };
		do_sockcall(regfile, GET_ARG0(regfile), v);
		break;
	}

	case ARCH_SYS_DEFAULT_EQ0:
		sc_ret_v(regfile, 0);
		break;

	case ARCH_SYS_DEFAULT_LE0:
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;


	case SYS_brk: {
		sc_brk(regfile);
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
	//	if (new_regs == NULL)
	//		new_regs = sc_mmap(regfile, len);
		sc_mmap(regfile, len);

		SC_BREADCRUMB_FL_OR(BC_FL_SC_THUNK);
	//	sc_breadcrumb_commit(&sc, GET_SYSRET(new_regs));
		sc_breadcrumb_commit(&sc, GET_SYSRET(regfile));
		goto skip_commit;
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
		goto skip_commit;

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
		break;

	case SYS_times:
		sc_new_regs(regfile);
		make_sym_by_arg(regfile, 0, TMS_SZ, "times");
	break;

	case SYS_ustat:
		/* int ustat(dev, ubuf) */
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET_S(new_regs) == -1)
			break;

		klee_assume_eq(GET_SYSRET(new_regs), 0);
		make_sym_by_arg(regfile, 1, USTAT_SZ, "ustatbuf");
		break;

	case SYS_sched_yield:
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
	case SYS_swapoff:
	case SYS_swapon:
	case SYS_ftruncate:
	case SYS_truncate:
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
	case SYS_setpriority:
	case SYS_setitimer:
	case SYS_personality:
	case SYS_fallocate:
	case SYS_ioperm:
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;

	/* begin socket code */
#define DECL_ARGS3 unsigned long v[]\
	= {GET_ARG0(regfile), GET_ARG1(regfile), GET_ARG2(regfile) }

	case SYS_socket: do_sockcall(regfile, SYS_SOCKET, NULL); break;
	case SYS_bind: do_sockcall(regfile, SYS_BIND, NULL); break;
	case SYS_connect: do_sockcall(regfile, SYS_CONNECT, NULL); break;
	case SYS_listen: do_sockcall(regfile, SYS_LISTEN, NULL); break;
	case SYS_getsockopt: do_sockcall(regfile, SYS_GETSOCKOPT, NULL); break;
	case SYS_shutdown: do_sockcall(regfile, SYS_SHUTDOWN, NULL); break;
	case SYS_socketpair: do_sockcall(regfile, SYS_SOCKETPAIR, NULL); break;
	case SYS_setsockopt: do_sockcall(regfile, SYS_SETSOCKOPT, NULL); break;
	case SYS_getpeername: {
		DECL_ARGS3; do_sockcall(regfile, SYS_GETPEERNAME, v); break; }
	case SYS_accept:
	case SYS_accept4: {
		DECL_ARGS3; do_sockcall(regfile, SYS_ACCEPT4, v); break; }
	case SYS_getsockname: {
		DECL_ARGS3; do_sockcall(regfile, SYS_GETSOCKNAME, v); break; }
	case SYS_sendto: {
		DECL_ARGS3; do_sockcall(regfile, SYS_SENDTO, v); break; }
	case SYS_sendmsg: {
		DECL_ARGS3; do_sockcall(regfile, SYS_SENDMSG, v); break; }
	case SYS_recvmsg: {
		DECL_ARGS3; do_sockcall(regfile, SYS_RECVMSG, v); break; }

	case SYS_recvfrom: {
		unsigned long v[] = {
			GET_ARG0(regfile),
			GET_ARG1(regfile),
			GET_ARG2(regfile),
			GET_ARG3(regfile),
			GET_ARG4(regfile),
			GET_ARG5(regfile) };
		do_sockcall(regfile, SYS_RECVFROM, v);
		break;
	}

	/***** end socket code */

	case SYS_getpriority:
	case SYS_semget:
		new_regs = sc_new_regs(regfile);
		break;

	case SYS_prlimit64:
		/* XXX: should mark limit parameter as symbolic */
		sc_ret_or(sc_new_regs(regfile), -1, 0);
		break;


	case SYS_pause: sc_ret_v(regfile, -1); break;
	case SYS_getitimer:
		new_regs = sc_new_regs(regfile);
		if (GET_SYSRET_S(new_regs) == -1)
			break;

		if (GET_ARG1_PTR(regfile) != NULL) {
			make_sym_by_arg(regfile, 1, ITIMERVAL_SZ, "itimer");
		}

		sc_ret_v_new(new_regs, 0);
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

	case SYS_execve: {
		int 	i;
		char	**argv;

		if (GET_ARG0_PTR(regfile) == NULL) {
			klee_ureport("execve NULL filename", "scbuf.err");
			sc_ret_v(regfile, -1);
			break;
		}

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
		make_sym_by_arg(regfile, 0, sizeof(uid_t), "res_id");
		make_sym_by_arg(regfile, 1, sizeof(uid_t), "res_eid");
		make_sym_by_arg(regfile, 2, sizeof(uid_t), "res_sid");
		sc_ret_v(regfile, 0);
		break;

	case SYS_eventfd2:	/* of COURSE there's an eventfd2 */
	case SYS_eventfd: {
		static int e_fd_c = 512;
		sc_ret_or(sc_new_regs(regfile),
			-1, /* error */
			++e_fd_c /* fake event fd */);
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

		make_sym_by_arg(regfile, 1, 4, "capget");
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
		make_sym_by_arg(regfile, 1, len, "listxattr");
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
		make_sym_by_arg(regfile, 1, len, "syslog");
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

		klee_assume_eq(GET_SYSRET(new_regs), 0);
		make_sym_by_arg(regfile, 1, STATFS_SZ, "statfs");

		sc_ret_v_new(new_regs, 0);
		break;

	case SYS_getxattr:
	case SYS_lgetxattr:
		if (GET_ARG2_PTR(regfile) == NULL) {
			sc_ret_v(regfile, -1);
			break;
		}
		make_sym_by_arg(regfile, 2, GET_ARG3(regfile), "getxattr");
		sc_ret_or(sc_new_regs(regfile), -1, GET_ARG3(regfile));
		break;

	case SYS_gettimeofday:
		if (GET_ARG0_PTR(regfile) == NULL) {
			sc_ret_v(regfile, -1);
			break;
		}

		new_regs = sc_new_regs(regfile);
		make_sym_by_arg(regfile, 0, TIMEVAL_SZ, "timeofday");
		sc_ret_or(new_regs, 0, -1);
		break;

	case SYS_set_robust_list:
	case SYS_clone:
	case SYS_set_tid_address:
	case SYS_vfork:
	case SYS_fork:
		proc_sc(&sc);
		break;

	case ARCH_SYS_UNSUPP:
	default:
		kmc_sc_bad(sc.sys_nr);
		klee_ureport("Unknown Syscall", "sc.err");
		sc_ret_v(regfile, -1);
		break;
	}

	last_sc = sc.sys_nr;
	klee_assert (!new_regs || sc_breadcrumb_is_newregs());

	if (sc_breadcrumb_is_newregs()) {
		/* ret value is stored in ktest regctx */
		sc_breadcrumb_commit(&sc, 0);
	} else if (
		klee_is_symbolic(GET_SYSRET(regfile))
		&& !sc_breadcrumb_is_thunk())
	{
		new_regs = sc_new_regs(regfile);
		sc_ret_v_new(new_regs, GET_SYSRET(regfile));
		sc_breadcrumb_commit(&sc, 0);
	} else {
		/* ret value was written concretely */
		sc_breadcrumb_commit(&sc, GET_SYSRET(regfile));
	}

skip_commit:
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

void sc_ret_v_new(void* regfile, ARCH_CAST v1)
{
	klee_assume_eq(GET_SYSRET(regfile), v1);
	/* I'm not sure why I had a sign cast here in the first place */
//	klee_assume_eq(GET_SYSRET_S(regfile), (ARCH_SIGN_CAST)v1);
	GET_SYSRET(regfile) = v1;
}
