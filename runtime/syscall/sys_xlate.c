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

int syscall_xlate(unsigned int sys_nr)
{
	 if (sys_nr > 511)
	 	return -1;

	if (sys_nr == ARM__NR_mmap2)
		return __NR_mmap;
// XXX: we'll need this when we start supporting
// mmap of concrete files with offsets..
//		return ARM_SYS_mmap2;

	return sysnr_arm2amd64[sys_nr];
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

static int sysnr_x86toamd64[512] =
{
#define __SYSCALL(x)	[X86##x] = x,
#include "unistd_amd64.h"
[X86__NR_fcntl64] = __NR_fcntl,
[X86__NR_fstat64] = __NR_fstat
};

int syscall_xlate(unsigned int sys_nr)
{
	 if (sys_nr > 511)
	 	return -1;

	if (sys_nr == X86__NR_mmap2)
		return X86_SYS_mmap2;

	return sysnr_x86toamd64[sys_nr];
}

#endif
