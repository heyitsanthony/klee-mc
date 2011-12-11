#include "syscalls.h"

#ifdef GUEST_ARCH_ARM

#include "arm_unistd.h"
#define INVALID_SYSCALL_NR	511
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
#define __SYSCALL(x,y)	[ARM##x] = x,	
#include <asm/unistd_64.h>
[ARM__NR_fcntl64] = __NR_fcntl,
[ARM__NR_fstat64] = __NR_fstat
};

int syscall_xlate(unsigned int sys_nr)
{
	 if (sys_nr > 511)
	 	return -1;

	if (sys_nr == ARM__NR_mmap2)
		return ARM_SYS_mmap2;

	return sysnr_arm2amd64[sys_nr];
}

#endif
