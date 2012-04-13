#include "klee/klee.h"
#include "syscalls.h"

#ifdef GUEST_ARCH_ARM
#ifdef GUEST_ARCH_AMD64
#error wtf
#endif
#include "arm_unistd.h"
#define INVALID_SYSCALL_NR	511
/* these calls are in amd64 but not arm-- create fake arm calls */
#define ARM__NR_process_vm_writev INVALID_SYSCALL_NR
#define ARM__NR_process_vm_readv INVALID_SYSCALL_NR
#define ARM__NR_mmap		INVALID_SYSCALL_NR
#define ARM__NR_putpmsg		INVALID_SYSCALL_NR
#define ARM__NR_getpmsg		INVALID_SYSCALL_NR
#define ARM__NR_afs_syscall	INVALID_SYSCALL_NR
#define ARM__NR_modify_ldt	INVALID_SYSCALL_NR
#define ARM__NR_security	INVALID_SYSCALL_NR
#define ARM__NR_arch_prctl	INVALID_SYSCALL_NR
#define ARM__NR_migrate_pages	INVALID_SYSCALL_NR
#define ARM__NR_epoll_ctl_old	INVALID_SYSCALL_NR
#define ARM__NR_epoll_wait_old	INVALID_SYSCALL_NR

static int sysnr_arm2amd64[512] =
{
#define __SYSCALL(x)	[ARM##x] = x,
#include "unistd_amd64.h"
[ARM__NR_fcntl64] = __NR_fcntl,
[ARM__NR_fstat64] = __NR_fstat
};

void syscall_xlate(struct sc_pkt* sc)
{
	int	ret;

	if (sc->sys_nr > 511) {
		sc->sys_nr = -1;
		return;
	}

	sc_set_32bit(sc);

	if (sc->sys_nr == ARM__NR_mmap2) {
		sc->sys_nr = __NR_mmap;
		return;
	}

	ret = sysnr_arm2amd64[sc->sys_nr];
	if (ret != 0 || sc->sys_nr == ARM__NR_read) {
		klee_print_expr("pure sysnr", sc->sys_nr);
		klee_print_expr("xlate sysnr", ret);
		klee_report_error(
			__FILE__, __LINE__,
			"Could not find appropriate translation for syscall",
			"scxlate.err");
	}

	sc->sys_nr = ret;
}

#endif

#ifdef GUEST_ARCH_X86

#include "x86_unistd.h"
#define INVALID_SYSCALL_NR	511
#define X86__NR_shmget		INVALID_SYSCALL_NR
#define X86__NR_shmat		INVALID_SYSCALL_NR
#define X86__NR_shmctl		INVALID_SYSCALL_NR

/* XXX these are done through the 'socketcall' syscall */
/* XXX TODO TODO implement socketcall TODO TODO XXX */
#define X86__NR_socket		INVALID_SYSCALL_NR
#define X86__NR_connect		INVALID_SYSCALL_NR
#define X86__NR_accept		INVALID_SYSCALL_NR
#define X86__NR_sendto		INVALID_SYSCALL_NR
#define X86__NR_recvfrom	INVALID_SYSCALL_NR
#define X86__NR_sendmsg		INVALID_SYSCALL_NR
#define X86__NR_recvmsg		INVALID_SYSCALL_NR
#define X86__NR_bind		INVALID_SYSCALL_NR
#define X86__NR_listen		INVALID_SYSCALL_NR
#define X86__NR_getsockname	INVALID_SYSCALL_NR
#define X86__NR_getpeername	INVALID_SYSCALL_NR
#define X86__NR_socketpair	INVALID_SYSCALL_NR
#define X86__NR_setsockopt	INVALID_SYSCALL_NR
#define X86__NR_getsockopt	INVALID_SYSCALL_NR
#define X86__NR_accept4		INVALID_SYSCALL_NR

#define X86__NR_semget		INVALID_SYSCALL_NR
#define X86__NR_semop		INVALID_SYSCALL_NR
#define X86__NR_semctl		INVALID_SYSCALL_NR
#define X86__NR_shmdt		INVALID_SYSCALL_NR

#define X86__NR_msgget		INVALID_SYSCALL_NR
#define X86__NR_msgsnd		INVALID_SYSCALL_NR
#define X86__NR_msgrcv		INVALID_SYSCALL_NR
#define X86__NR_msgctl		INVALID_SYSCALL_NR

#define X86__NR_arch_prctl	INVALID_SYSCALL_NR
#define X86__NR_shutdown	INVALID_SYSCALL_NR
#define X86__NR_tuxcall		INVALID_SYSCALL_NR
#define X86__NR_security	INVALID_SYSCALL_NR

#define X86__NR_epoll_ctl_old	INVALID_SYSCALL_NR
#define X86__NR_epoll_wait_old	INVALID_SYSCALL_NR
#define X86__NR_semtimedop	INVALID_SYSCALL_NR
#define X86__NR_newfstatat	INVALID_SYSCALL_NR
//#define X86__NR_		INVALID_SYSCALL_NR
//#define X86__NR_		INVALID_SYSCALL_NR
//#define X86__NR_		INVALID_SYSCALL_NR

/* works just like amd64, just with different pointer sizes */
static int sysnr_x86toamd64[512] =
{
#define __SYSCALL(x)	[X86##x] = x,
#include "unistd_amd64.h"
[X86__NR_fcntl64] = __NR_fcntl,
[X86__NR_stat64] = __NR_stat,
[X86__NR_lstat64] = __NR_lstat,
[X86__NR_fstat64] = __NR_fstat,
[X86__NR_chown32] = __NR_chown,
[X86__NR_setuid32] = __NR_setuid,
[X86__NR_setgid32] = __NR_setgid,
[X86__NR_setfsuid32] = __NR_setfsuid,
[X86__NR_setfsgid32] = __NR_setfsgid
};

/* different structure sizes for 32-bit calls */
static int sysnr_x86toamd64_32bit[512] =
{
[X86__NR_lchown32] = __NR_lchown,
[X86__NR_getuid32] = __NR_getuid,
[X86__NR_getgid32] = __NR_getgid,
[X86__NR_geteuid32] = __NR_geteuid,
[X86__NR_getegid32] = __NR_getegid,
[X86__NR_setreuid32] = __NR_setreuid,
[X86__NR_setregid32] = __NR_setregid,
[X86__NR_getgroups32] = __NR_getgroups,
[X86__NR_setgroups32] = __NR_setgroups,
[X86__NR_fchown32] = __NR_fchown,
[X86__NR_setresuid32] = __NR_setresuid,
[X86__NR_getresuid32] = __NR_getresuid,
[X86__NR_setresgid32] = __NR_setresgid,
[X86__NR_getresgid32] = __NR_getresgid,
__SYSCALL(__NR_stat)
__SYSCALL(__NR_lstat)
__SYSCALL(__NR_fstat)
};

void syscall_xlate(struct sc_pkt* sc)
{
	int	ret;

	if (sc->sys_nr > 511) {
		sc->sys_nr = -1;
	 	return;
	}

	if (sc->sys_nr == X86__NR_mmap2) {
		sc->sys_nr = X86_SYS_mmap2;
		return;
	}

	ret = sysnr_x86toamd64_32bit[sc->sys_nr];
	if (ret != 0 || sc->sys_nr == X86__NR_read) {
		sc_set_32bit(sc);
		sc->sys_nr = ret;
		return;
	}


	ret = sysnr_x86toamd64[sc->sys_nr];
	if (ret == 0 && sc->sys_nr != X86__NR_read) {
		klee_print_expr("pure sysnr", sc->sys_nr);
		klee_print_expr("xlate sysnr", ret);
		klee_report_error(
			__FILE__, __LINE__,
			"Could not find appropriate translation for syscall",
			"scxlate.err");
	}

	sc->sys_nr = ret;
	return;
}

#endif
