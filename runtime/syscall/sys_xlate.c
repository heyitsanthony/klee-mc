#include "klee/klee.h"
#include "syscalls.h"

#ifdef GUEST_ARCH_ARM
#ifdef GUEST_ARCH_AMD64
#error wtf
#endif
//#include "arm_unistd.h"
#define INVALID_SYSCALL_NR	511
#if 0
/* these calls are in amd64 but not arm-- create fake arm calls */
#define ARM__NR_process_vm_writev INVALID_SYSCALL_NR
#define ARM__NR_process_vm_readv INVALID_SYSCALL_NR
//#define ARM__NR_mmap		INVALID_SYSCALL_NR
#define ARM__NR_putpmsg		INVALID_SYSCALL_NR
#define ARM__NR_getpmsg		INVALID_SYSCALL_NR
#define ARM__NR_afs_syscall	INVALID_SYSCALL_NR
#define ARM__NR_modify_ldt	INVALID_SYSCALL_NR
#define ARM__NR_security	INVALID_SYSCALL_NR
#define ARM__NR_arch_prctl	INVALID_SYSCALL_NR
#define ARM__NR_migrate_pages	INVALID_SYSCALL_NR
#define ARM__NR_epoll_ctl_old	INVALID_SYSCALL_NR
#define ARM__NR_epoll_wait_old	INVALID_SYSCALL_NR
#endif

#define __SYSCALL(x)
#include "unistd_amd64.h"
#undef __SYSCALL

#define __SYSCALL(x)
#include "arm_unistd.h"

static int sysnr_arm2amd64[512] =
{
[ARM__NR_restart_syscall] = __NR_restart_syscall,
[ARM__NR_exit] = __NR_exit,
[ARM__NR_fork] = __NR_fork,
[ARM__NR_read] = __NR_read,
[ARM__NR_write] = __NR_write,
[ARM__NR_open] = __NR_open,
[ARM__NR_close] = __NR_close,
[ARM__NR_creat] = __NR_creat,
[ARM__NR_link] = __NR_link,
[ARM__NR_unlink] = __NR_unlink,
[ARM__NR_execve] = __NR_execve,
[ARM__NR_chdir] = __NR_chdir,
[ARM__NR_time] = __NR_time,
[ARM__NR_mknod] = __NR_mknod,
[ARM__NR_chmod] = __NR_chmod,
[ARM__NR_lchown] = __NR_lchown,
[ARM__NR_lseek] = __NR_lseek,
[ARM__NR_getpid] = __NR_getpid,
[ARM__NR_mount] = __NR_mount,
[ARM__NR_umount] = ARCH_SYS_DEFAULT_LE0,
[ARM__NR_setuid] = __NR_setuid,
[ARM__NR_getuid] = __NR_getuid,
[ARM__NR_stime] = ARCH_SYS_UNSUPP,
[ARM__NR_ptrace] = __NR_ptrace,
[ARM__NR_alarm] = __NR_alarm,
[ARM__NR_pause] = __NR_pause,
[ARM__NR_utime] = __NR_utime,
[ARM__NR_access] = __NR_access,
[ARM__NR_nice] = ARCH_SYS_DEFAULT_EQ0,
[ARM__NR_sync] = __NR_sync,
[ARM__NR_kill] = __NR_kill,
[ARM__NR_rename] = __NR_rename,
[ARM__NR_mkdir] = __NR_mkdir,
[ARM__NR_rmdir] = __NR_rmdir,
[ARM__NR_dup] = __NR_dup,
[ARM__NR_pipe] = __NR_pipe,
[ARM__NR_times] = __NR_times,
					/* 44 was sys_prof */
[ARM__NR_brk] = __NR_brk,
[ARM__NR_setgid] = __NR_setgid,
[ARM__NR_getgid] = __NR_getgid,
					/* 48 was sys_signal */
[ARM__NR_geteuid] = __NR_geteuid,
[ARM__NR_getegid] = __NR_getegid,
[ARM__NR_acct] = __NR_acct,
[ARM__NR_umount2] = __NR_umount2,
					/* 53 was sys_lock */
[ARM__NR_ioctl] = __NR_ioctl,
[ARM__NR_fcntl] = __NR_fcntl,
					/* 56 was sys_mpx */
[ARM__NR_setpgid] = __NR_setpgid,
					/* 58 was sys_ulimit */
					/* 59 was sys_olduname */
[ARM__NR_umask] = __NR_umask,
[ARM__NR_chroot] = __NR_chroot,
[ARM__NR_ustat] = __NR_ustat,
[ARM__NR_dup2] = __NR_dup2,
[ARM__NR_getppid] = __NR_getppid,
[ARM__NR_getpgrp] = __NR_getpgrp,
[ARM__NR_setsid] = __NR_setsid,
[ARM__NR_sigaction] = __NR_rt_sigaction,
					/* 68 was sys_sgetmask */
					/* 69 was sys_ssetmask */
[ARM__NR_setreuid] = __NR_setreuid,
[ARM__NR_setregid] = __NR_setregid,
[ARM__NR_sigsuspend] = __NR_rt_sigsuspend,
[ARM__NR_sigpending] = __NR_rt_sigpending,
[ARM__NR_sethostname] = __NR_sethostname,
[ARM__NR_setrlimit] = __NR_setrlimit,
[ARM__NR_getrlimit	/* Back compat 2GB limited rlimit */] = __NR_getrlimit	/* Back compat 2GB limited rlimit */,
[ARM__NR_getrusage] = __NR_getrusage,
[ARM__NR_gettimeofday] = __NR_gettimeofday,
[ARM__NR_settimeofday] = __NR_settimeofday,
[ARM__NR_getgroups] = __NR_getgroups,
[ARM__NR_setgroups] = __NR_setgroups,
//[ARM__NR_select] = __NR_select,
[ARM__NR_symlink] = __NR_symlink,
					/* 84 was sys_lstat */
[ARM__NR_readlink] = __NR_readlink,
[ARM__NR_uselib] = __NR_uselib,
[ARM__NR_swapon] = __NR_swapon,
[ARM__NR_reboot] = __NR_reboot,
[ARM__NR_readdir] = ARCH_SYS_UNSUPP,
[ARM__NR_mmap] = __NR_mmap,
[ARM__NR_munmap] = __NR_munmap,
[ARM__NR_truncate] = __NR_truncate,
[ARM__NR_ftruncate] = __NR_ftruncate,
[ARM__NR_fchmod] = __NR_fchmod,
[ARM__NR_fchown] = __NR_fchown,
[ARM__NR_getpriority] = __NR_getpriority,
[ARM__NR_setpriority] = __NR_setpriority,
					/* 98 was sys_profil */
[ARM__NR_statfs] = __NR_statfs,
[ARM__NR_fstatfs] = __NR_fstatfs,
[ARM__NR_ioperm] = __NR_ioperm,
[ARM__NR_socketcall] = ARCH_SYS_UNSUPP,
[ARM__NR_syslog] = __NR_syslog,
[ARM__NR_setitimer] = __NR_setitimer,
[ARM__NR_getitimer] = __NR_getitimer,
[ARM__NR_stat] = __NR_stat,
[ARM__NR_lstat] = __NR_lstat,
[ARM__NR_fstat] = __NR_fstat,
					/* 109 was sys_uname */
/* 110 was sys_iopl */
[ARM__NR_iopl] = __NR_iopl,
[ARM__NR_vhangup] = __NR_vhangup,
					/* 112 was sys_idle */
[ARM__NR_syscall] = ARCH_SYS_UNSUPP,
[ARM__NR_wait4] = __NR_wait4,
[ARM__NR_swapoff] = __NR_swapoff,
[ARM__NR_sysinfo] = __NR_sysinfo,
[ARM__NR_ipc] = ARCH_SYS_UNSUPP,
[ARM__NR_fsync] = __NR_fsync,
[ARM__NR_sigreturn] = __NR_rt_sigreturn,
[ARM__NR_clone] = __NR_clone,
[ARM__NR_setdomainname] = __NR_setdomainname,
[ARM__NR_uname] = __NR_uname,
					/* 123 was sys_modify_ldt */
[ARM__NR_adjtimex] = __NR_adjtimex,
[ARM__NR_mprotect] = __NR_mprotect,
[ARM__NR_sigprocmask] = __NR_rt_sigprocmask,
[ARM__NR_create_module ] = __NR_create_module ,
[ARM__NR_init_module] = __NR_init_module,
[ARM__NR_delete_module] = __NR_delete_module,
[ARM__NR_get_kernel_syms] = __NR_get_kernel_syms,
					/* 130 was sys_get_kernel_syms */
[ARM__NR_quotactl] = __NR_quotactl,
[ARM__NR_getpgid] = __NR_getpgid,
[ARM__NR_fchdir] = __NR_fchdir,
[ARM__NR_bdflush] = ARCH_SYS_DEFAULT_EQ0,
[ARM__NR_sysfs] = __NR_sysfs,
[ARM__NR_personality] = __NR_personality,
					/* 137 was sys_afs_syscall */
[ARM__NR_setfsuid] = __NR_setfsuid,
[ARM__NR_setfsgid] = __NR_setfsgid,
[ARM__NR__llseek] = __NR_lseek,
[ARM__NR_getdents] = __NR_getdents,
[ARM__NR__newselect] = __NR_select,
[ARM__NR_flock] = __NR_flock,
[ARM__NR_msync] = __NR_msync,
[ARM__NR_readv] = __NR_readv,
[ARM__NR_writev] = __NR_writev,
[ARM__NR_getsid] = __NR_getsid,
[ARM__NR_fdatasync] = __NR_fdatasync,
[ARM__NR__sysctl] = __NR__sysctl,
[ARM__NR_mlock] = __NR_mlock,
[ARM__NR_munlock] = __NR_munlock,
[ARM__NR_mlockall] = __NR_mlockall,
[ARM__NR_munlockall] = __NR_munlockall,
[ARM__NR_sched_setparam] = __NR_sched_setparam,
[ARM__NR_sched_getparam] = __NR_sched_getparam,
[ARM__NR_sched_setscheduler] = __NR_sched_setscheduler,
[ARM__NR_sched_getscheduler] = __NR_sched_getscheduler,
[ARM__NR_sched_yield] = __NR_sched_yield,
[ARM__NR_sched_get_priority_max] = __NR_sched_get_priority_max,
[ARM__NR_sched_get_priority_min] = __NR_sched_get_priority_min,
[ARM__NR_sched_rr_get_interval] = __NR_sched_rr_get_interval,
[ARM__NR_nanosleep] = __NR_nanosleep,
[ARM__NR_mremap] = __NR_mremap,
[ARM__NR_setresuid] = __NR_setresuid,
[ARM__NR_getresuid] = __NR_getresuid,
					/* 166 was sys_vm86 */
[ARM__NR_query_module] = __NR_query_module,
[ARM__NR_poll] = __NR_poll,
[ARM__NR_nfsservctl] = __NR_nfsservctl,
[ARM__NR_setresgid] = __NR_setresgid,
[ARM__NR_getresgid] = __NR_getresgid,
[ARM__NR_prctl] = __NR_prctl,
[ARM__NR_rt_sigreturn] = __NR_rt_sigreturn,
[ARM__NR_rt_sigaction] = __NR_rt_sigaction,
[ARM__NR_rt_sigprocmask] = __NR_rt_sigprocmask,
[ARM__NR_rt_sigpending] = __NR_rt_sigpending,
[ARM__NR_rt_sigtimedwait] = __NR_rt_sigtimedwait,
[ARM__NR_rt_sigqueueinfo] = __NR_rt_sigqueueinfo,
[ARM__NR_rt_sigsuspend] = __NR_rt_sigsuspend,
[ARM__NR_pread64] = __NR_pread64,
[ARM__NR_pwrite64] = __NR_pwrite64,
[ARM__NR_chown] = __NR_chown,
[ARM__NR_getcwd] = __NR_getcwd,
[ARM__NR_capget] = __NR_capget,
[ARM__NR_capset] = __NR_capset,
[ARM__NR_sigaltstack] = __NR_sigaltstack,
[ARM__NR_sendfile] = __NR_sendfile,
					/* 188 reserved */
					/* 189 reserved */
[ARM__NR_vfork] = __NR_vfork,
[ARM__NR_ugetrlimit] = __NR_getrlimit	/* SuS compliant getrlimit */,
[ARM__NR_mmap2] = ARM_SYS_mmap2,
[ARM__NR_truncate64] = __NR_truncate,
[ARM__NR_ftruncate64] = __NR_ftruncate,
[ARM__NR_stat64] = __NR_stat,
[ARM__NR_lstat64] = __NR_lstat,
[ARM__NR_fstat64] = __NR_fstat,
[ARM__NR_lchown32] = __NR_lchown,
[ARM__NR_getuid32] = __NR_getuid,
[ARM__NR_getgid32] = __NR_getgid,
[ARM__NR_geteuid32] = __NR_geteuid,
[ARM__NR_getegid32] = __NR_getegid,
[ARM__NR_setreuid32] = __NR_setreuid,
[ARM__NR_setregid32] = __NR_setregid,
[ARM__NR_getgroups32] = ARCH_SYS_UNSUPP,
[ARM__NR_setgroups32] = __NR_setgroups,
[ARM__NR_fchown32] = __NR_fchown,
[ARM__NR_setresuid32] = __NR_setresuid,
[ARM__NR_getresuid32] = __NR_getresuid,
[ARM__NR_setresgid32] = __NR_setresgid,
[ARM__NR_getresgid32] = __NR_getresgid,
[ARM__NR_chown32] = __NR_chown,
[ARM__NR_setuid32] = __NR_setuid,
[ARM__NR_setgid32] = __NR_setgid,
[ARM__NR_setfsuid32] = __NR_setfsuid,
[ARM__NR_setfsgid32] = __NR_setfsgid,
[ARM__NR_getdents64] = __NR_getdents64,
[ARM__NR_pivot_root] = __NR_pivot_root,
[ARM__NR_mincore] = __NR_mincore,
[ARM__NR_madvise] = __NR_madvise,
[ARM__NR_fcntl64] = __NR_fcntl,
[ARM__NR_tuxcall] = __NR_tuxcall,
					/* 223 is unused */
[ARM__NR_gettid] = __NR_gettid,
[ARM__NR_readahead] = __NR_readahead,
[ARM__NR_setxattr] = __NR_setxattr,
[ARM__NR_lsetxattr] = __NR_lsetxattr,
[ARM__NR_fsetxattr] = __NR_fsetxattr,
[ARM__NR_getxattr] = __NR_getxattr,
[ARM__NR_lgetxattr] = __NR_lgetxattr,
[ARM__NR_fgetxattr] = __NR_fgetxattr,
[ARM__NR_listxattr] = __NR_listxattr,
[ARM__NR_llistxattr] = __NR_llistxattr,
[ARM__NR_flistxattr] = __NR_flistxattr,
[ARM__NR_removexattr] = __NR_removexattr,
[ARM__NR_lremovexattr] = __NR_lremovexattr,
[ARM__NR_fremovexattr] = __NR_fremovexattr,
[ARM__NR_tkill] = __NR_tkill,
[ARM__NR_sendfile64] = __NR_sendfile,
[ARM__NR_futex] = __NR_futex,
[ARM__NR_sched_setaffinity] = __NR_sched_setaffinity,
[ARM__NR_sched_getaffinity] = __NR_sched_getaffinity,
[ARM__NR_io_setup] = __NR_io_setup,
[ARM__NR_io_destroy] = __NR_io_destroy,
[ARM__NR_io_getevents] = __NR_io_getevents,
[ARM__NR_io_submit] = __NR_io_submit,
[ARM__NR_io_cancel] = __NR_io_cancel,
[ARM__NR_exit_group] = __NR_exit_group,
[ARM__NR_lookup_dcookie] = __NR_lookup_dcookie,
[ARM__NR_epoll_create] = __NR_epoll_create,
[ARM__NR_epoll_ctl] = __NR_epoll_ctl,
[ARM__NR_epoll_wait] = __NR_epoll_wait,
[ARM__NR_remap_file_pages] = __NR_remap_file_pages,
[ARM__NR_set_thread_area] = __NR_set_thread_area,
[ARM__NR_get_thread_area] = __NR_get_thread_area,
[ARM__NR_set_tid_address] = __NR_set_tid_address,
[ARM__NR_timer_create] = __NR_timer_create,
[ARM__NR_timer_settime] = __NR_timer_settime,
[ARM__NR_timer_gettime] = __NR_timer_gettime,
[ARM__NR_timer_getoverrun] = __NR_timer_getoverrun,
[ARM__NR_timer_delete] = __NR_timer_delete,
[ARM__NR_clock_settime] = __NR_clock_settime,
[ARM__NR_clock_gettime] = __NR_clock_gettime,
[ARM__NR_clock_getres] = __NR_clock_getres,
[ARM__NR_clock_nanosleep] = __NR_clock_nanosleep,
[ARM__NR_statfs64] = __NR_statfs,
[ARM__NR_fstatfs64] = __NR_fstatfs,
[ARM__NR_tgkill] = __NR_tgkill,
[ARM__NR_utimes] = __NR_utimes,
[ARM__NR_fadvise64] = __NR_fadvise64,
[ARM__NR_pciconfig_iobase] = ARCH_SYS_UNSUPP,
[ARM__NR_pciconfig_read] = ARCH_SYS_UNSUPP,
[ARM__NR_pciconfig_write] = ARCH_SYS_UNSUPP,
[ARM__NR_mq_open] = __NR_mq_open,
[ARM__NR_mq_unlink] = __NR_mq_unlink,
[ARM__NR_mq_timedsend] = __NR_mq_timedsend,
[ARM__NR_mq_timedreceive] = __NR_mq_timedreceive,
[ARM__NR_mq_notify] = __NR_mq_notify,
[ARM__NR_mq_getsetattr] = __NR_mq_getsetattr,
[ARM__NR_waitid] = __NR_waitid,
[ARM__NR_socket] = __NR_socket,
[ARM__NR_bind] = __NR_bind,
[ARM__NR_connect] = __NR_connect,
[ARM__NR_listen] = __NR_listen,
[ARM__NR_accept] = __NR_accept,
[ARM__NR_getsockname] = __NR_getsockname,
[ARM__NR_getpeername] = __NR_getpeername,
[ARM__NR_socketpair] = __NR_socketpair,
[ARM__NR_send] = __NR_sendto,	/* XXX: might break-- AJR */
[ARM__NR_sendto] = __NR_sendto,
[ARM__NR_recv] = __NR_recvfrom, /* XXX: might break-- AJR */
[ARM__NR_recvfrom] = __NR_recvfrom,
[ARM__NR_shutdown] = __NR_shutdown,
[ARM__NR_setsockopt] = __NR_setsockopt,
[ARM__NR_getsockopt] = __NR_getsockopt,
[ARM__NR_sendmsg] = __NR_sendmsg,
[ARM__NR_recvmsg] = __NR_recvmsg,
[ARM__NR_semop] = __NR_semop,
[ARM__NR_semget] = __NR_semget,
[ARM__NR_semctl] = __NR_semctl,
[ARM__NR_msgsnd] = __NR_msgsnd,
[ARM__NR_msgrcv] = __NR_msgrcv,
[ARM__NR_msgget] = __NR_msgget,
[ARM__NR_msgctl] = __NR_msgctl,
[ARM__NR_shmat] = __NR_shmat,
[ARM__NR_shmdt] = __NR_shmdt,
[ARM__NR_shmget] = __NR_shmget,
[ARM__NR_shmctl] = __NR_shmctl,
[ARM__NR_add_key] = __NR_add_key,
[ARM__NR_request_key] = __NR_request_key,
[ARM__NR_keyctl] = __NR_keyctl,
[ARM__NR_semtimedop] = __NR_semtimedop,
[ARM__NR_vserver] = __NR_vserver,
[ARM__NR_ioprio_set] = __NR_ioprio_set,
[ARM__NR_ioprio_get] = __NR_ioprio_get,
[ARM__NR_inotify_init] = __NR_inotify_init,
[ARM__NR_inotify_add_watch] = __NR_inotify_add_watch,
[ARM__NR_inotify_rm_watch] = __NR_inotify_rm_watch,
[ARM__NR_mbind] = __NR_mbind,
[ARM__NR_get_mempolicy] = __NR_get_mempolicy,
[ARM__NR_set_mempolicy] = __NR_set_mempolicy,
[ARM__NR_openat] = __NR_openat,
[ARM__NR_mkdirat] = __NR_mkdirat,
[ARM__NR_mknodat] = __NR_mknodat,
[ARM__NR_fchownat] = __NR_fchownat,
[ARM__NR_futimesat] = __NR_futimesat,
[ARM__NR_fstatat64] = __NR_newfstatat,
[ARM__NR_unlinkat] = __NR_unlinkat,
[ARM__NR_renameat] = __NR_renameat,
[ARM__NR_linkat] = __NR_linkat,
[ARM__NR_symlinkat] = __NR_symlinkat,
[ARM__NR_readlinkat] = __NR_readlinkat,
[ARM__NR_fchmodat] = __NR_fchmodat,
[ARM__NR_faccessat] = __NR_faccessat,
[ARM__NR_pselect6] = __NR_pselect6,
[ARM__NR_ppoll] = __NR_ppoll,
[ARM__NR_unshare] = __NR_unshare,
[ARM__NR_set_robust_list] = __NR_set_robust_list,
[ARM__NR_get_robust_list] = __NR_get_robust_list,
[ARM__NR_splice] = __NR_splice,
//[ARM__NR_arm_sync_file_range] = __NR_arm_sync_file_range,
//[ARM__NR_sync_file_range2	ARM__NR_arm_sync_file_range] = __NR_sync_file_range2	ARM__NR_arm_sync_file_range,
[ARM__NR_sync_file_range] = __NR_sync_file_range,
[ARM__NR_tee] = __NR_tee,
[ARM__NR_vmsplice] = __NR_vmsplice,
[ARM__NR_move_pages] = __NR_move_pages,
[ARM__NR_getcpu] = __NR_getcpu,
[ARM__NR_epoll_pwait] = __NR_epoll_pwait,
[ARM__NR_kexec_load] = __NR_kexec_load,
[ARM__NR_utimensat] = __NR_utimensat,
[ARM__NR_signalfd] = __NR_signalfd,
[ARM__NR_timerfd_create] = __NR_timerfd_create,
[ARM__NR_eventfd] = __NR_eventfd,
[ARM__NR_fallocate] = __NR_fallocate,
[ARM__NR_timerfd_settime] = __NR_timerfd_settime,
[ARM__NR_timerfd_gettime] = __NR_timerfd_gettime,
[ARM__NR_signalfd4] = __NR_signalfd4,
[ARM__NR_eventfd2] = __NR_eventfd2,
[ARM__NR_epoll_create1] = __NR_epoll_create1,
[ARM__NR_dup3] = __NR_dup3,
[ARM__NR_pipe2] = __NR_pipe2,
[ARM__NR_inotify_init1] = __NR_inotify_init1,
[ARM__NR_preadv] = __NR_preadv,
[ARM__NR_pwritev] = __NR_pwritev,
[ARM__NR_rt_tgsigqueueinfo] = __NR_rt_tgsigqueueinfo,
[ARM__NR_perf_event_open] = __NR_perf_event_open,
[ARM__NR_recvmmsg] = __NR_recvmmsg,
[ARM__NR_accept4] = __NR_accept4,
[ARM__NR_fanotify_init] = __NR_fanotify_init,
[ARM__NR_fanotify_mark] = __NR_fanotify_mark,
[ARM__NR_prlimit64] = __NR_prlimit64,
[ARM__NR_name_to_handle_at] = __NR_name_to_handle_at,
[ARM__NR_open_by_handle_at] = __NR_open_by_handle_at,
[ARM__NR_clock_adjtime] = __NR_clock_adjtime,
[ARM__NR_syncfs] = __NR_syncfs,
[ARM__NR_sendmmsg] = __NR_sendmmsg,
[ARM__NR_setns] = __NR_setns,
};

void syscall_xlate(struct sc_pkt* sc)
{
	int	ret;

	if (sc->sys_nr > 511) {
		sc->sys_nr = -1;
		return;
	}

	sc_set_32bit(sc);
	ret = sysnr_arm2amd64[sc->sys_nr];
	if (ret == 0 && sc->sys_nr != ARM__NR_read) {
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

#include "unistd_amd64.h"

/* works just like amd64, just with different pointer sizes */
static int sysnr_x86toamd64[512] =
{
[X86__NR_restart_syscall] = INVALID_SYSCALL_NR, 
[X86__NR_exit] = __NR_exit,
[X86__NR_fork] = __NR_fork,
[X86__NR_read] = __NR_read,
[X86__NR_write] = __NR_write,
[X86__NR_open] = __NR_open,
[X86__NR_close] = __NR_close,
[X86__NR_waitpid] = ARCH_SYS_UNSUPP,
[X86__NR_creat] = __NR_creat,
[X86__NR_link] = __NR_link,
[X86__NR_unlink] = __NR_unlink,
[X86__NR_execve] = __NR_execve,
[X86__NR_chdir] = __NR_chdir,
[X86__NR_time] = __NR_time,
[X86__NR_mknod] = __NR_mknod,
[X86__NR_chmod] = __NR_chmod,
[X86__NR_lchown] = __NR_lchown,
[X86__NR_break] = __NR_brk,
[X86__NR_oldstat] = ARCH_SYS_UNSUPP,
[X86__NR_lseek] = __NR_lseek,
[X86__NR_getpid] = __NR_getpid,
[X86__NR_mount] = __NR_mount,
[X86__NR_umount] = __NR_umount2,
[X86__NR_setuid] = __NR_setuid,
[X86__NR_getuid] = __NR_getuid,
[X86__NR_stime] = ARCH_SYS_DEFAULT_LE0,
[X86__NR_ptrace] = __NR_ptrace,
[X86__NR_alarm] = __NR_alarm,
[X86__NR_oldfstat] = ARCH_SYS_UNSUPP,
[X86__NR_pause] = __NR_pause,
[X86__NR_utime] = __NR_utime,
[X86__NR_stty] = ARCH_SYS_UNSUPP,
[X86__NR_gtty] = ARCH_SYS_UNSUPP,
[X86__NR_access] = __NR_access,
[X86__NR_nice] = ARCH_SYS_DEFAULT_EQ0,
[X86__NR_ftime] = ARCH_SYS_UNSUPP,
[X86__NR_sync] = __NR_sync,
[X86__NR_kill] = __NR_kill,
[X86__NR_rename] = __NR_rename,
[X86__NR_mkdir] = __NR_mkdir,
[X86__NR_rmdir] = __NR_rmdir,
[X86__NR_dup] = __NR_dup,
[X86__NR_pipe] = __NR_pipe,
[X86__NR_times] = __NR_times,
[X86__NR_prof] = ARCH_SYS_UNSUPP,
[X86__NR_brk] = __NR_brk,
[X86__NR_setgid] = __NR_setgid,
[X86__NR_getgid] = __NR_getgid,
[X86__NR_signal] = ARCH_SYS_UNSUPP,
[X86__NR_geteuid] = __NR_geteuid,
[X86__NR_getegid] = __NR_getegid,
[X86__NR_acct] = __NR_acct,
[X86__NR_umount2] = __NR_umount2,
[X86__NR_lock] = ARCH_SYS_UNSUPP,
[X86__NR_ioctl] = __NR_ioctl,
[X86__NR_fcntl] = __NR_fcntl,
[X86__NR_mpx] = ARCH_SYS_UNSUPP,
[X86__NR_setpgid] = __NR_setpgid,
[X86__NR_ulimit] = ARCH_SYS_UNSUPP,
[X86__NR_oldolduname] = ARCH_SYS_UNSUPP,
[X86__NR_umask] = __NR_umask,
[X86__NR_chroot] = __NR_chroot,
[X86__NR_ustat] = __NR_ustat,
[X86__NR_dup2] = __NR_dup2,
[X86__NR_getppid] = __NR_getppid,
[X86__NR_getpgrp] = __NR_getpgrp,
[X86__NR_setsid] = __NR_setsid,
[X86__NR_sigaction] = __NR_rt_sigaction,
[X86__NR_sgetmask] = ARCH_SYS_UNSUPP,
[X86__NR_ssetmask] = ARCH_SYS_UNSUPP,
[X86__NR_setreuid] = __NR_setreuid,
[X86__NR_setregid] = __NR_setregid,
[X86__NR_sigsuspend] = __NR_rt_sigsuspend,
[X86__NR_sigpending] = __NR_rt_sigpending,
[X86__NR_sethostname] = __NR_sethostname,
[X86__NR_setrlimit] = __NR_setrlimit,
[X86__NR_getrlimit] = __NR_getrlimit,
[X86__NR_getrusage] = __NR_getrusage,
[X86__NR_gettimeofday] = __NR_gettimeofday,
[X86__NR_settimeofday] = __NR_settimeofday,
[X86__NR_getgroups] = __NR_getgroups,
[X86__NR_setgroups] = __NR_setgroups,
[X86__NR_select] = __NR_select,
[X86__NR_symlink] = __NR_symlink,
[X86__NR_oldlstat] = __NR_lstat,
[X86__NR_readlink] = __NR_readlink,
[X86__NR_uselib] = __NR_uselib,
[X86__NR_swapon] = __NR_swapon,
[X86__NR_reboot] = __NR_reboot,
[X86__NR_readdir] = ARCH_SYS_UNSUPP,
[X86__NR_mmap] = __NR_mmap,
[X86__NR_munmap] = __NR_munmap,
[X86__NR_truncate] = __NR_truncate,
[X86__NR_ftruncate] = __NR_ftruncate,
[X86__NR_fchmod] = __NR_fchmod,
[X86__NR_fchown] = __NR_fchown,
[X86__NR_getpriority] = __NR_getpriority,
[X86__NR_setpriority] = __NR_setpriority,
[X86__NR_profil] = ARCH_SYS_UNSUPP, /* complicated semantics */
[X86__NR_statfs] = __NR_statfs,
[X86__NR_fstatfs] = __NR_fstatfs,
[X86__NR_ioperm] = __NR_ioperm,
[X86__NR_socketcall] = ARCH_SYS_UNSUPP,
[X86__NR_syslog] = __NR_syslog,
[X86__NR_setitimer] = __NR_setitimer,
[X86__NR_getitimer] = __NR_getitimer,
[X86__NR_stat] = __NR_stat,
[X86__NR_lstat] = __NR_lstat,
[X86__NR_fstat] = __NR_fstat,
[X86__NR_olduname] = __NR_uname,
[X86__NR_iopl] = __NR_iopl,
[X86__NR_vhangup] = __NR_vhangup,
[X86__NR_idle] = ARCH_SYS_DEFAULT_LE0,
[X86__NR_vm86old] = ARCH_SYS_UNSUPP,
[X86__NR_wait4] = __NR_wait4,
[X86__NR_swapoff] = __NR_swapoff,
[X86__NR_sysinfo] = __NR_sysinfo,
[X86__NR_ipc] = ARCH_SYS_UNSUPP,
[X86__NR_fsync] = __NR_fsync,
[X86__NR_sigreturn] = __NR_rt_sigreturn,
[X86__NR_clone] = __NR_clone,
[X86__NR_setdomainname] = __NR_setdomainname,
[X86__NR_uname] = __NR_uname,
[X86__NR_modify_ldt] = __NR_modify_ldt,
[X86__NR_adjtimex] = __NR_adjtimex,
[X86__NR_mprotect] = __NR_mprotect,
[X86__NR_sigprocmask] = __NR_rt_sigprocmask,
[X86__NR_create_module] = __NR_create_module,
[X86__NR_init_module] = __NR_init_module,
[X86__NR_delete_module] = __NR_delete_module,
[X86__NR_get_kernel_syms] = __NR_get_kernel_syms,
[X86__NR_quotactl] = __NR_quotactl,
[X86__NR_getpgid] = __NR_getpgid,
[X86__NR_fchdir] = __NR_fchdir,
[X86__NR_bdflush] = ARCH_SYS_DEFAULT_EQ0,
[X86__NR_sysfs] = __NR_sysfs,
[X86__NR_personality] = __NR_personality,
[X86__NR_afs_syscall] = __NR_afs_syscall,
[X86__NR_setfsuid] = __NR_setfsuid,
[X86__NR_setfsgid] = __NR_setfsgid,
[X86__NR__llseek] = __NR_lseek,
[X86__NR_getdents] = __NR_getdents,
[X86__NR_newselect] = __NR_select,
[X86__NR_flock] = __NR_flock,
[X86__NR_msync] = __NR_msync,
[X86__NR_readv] = __NR_readv,
[X86__NR_writev] = __NR_writev,
[X86__NR_getsid] = __NR_getsid,
[X86__NR_fdatasync] = __NR_fdatasync,
[X86__NR__sysctl] = __NR__sysctl,
[X86__NR_mlock] = __NR_mlock,
[X86__NR_munlock] = __NR_munlock,
[X86__NR_mlockall] = __NR_mlockall,
[X86__NR_munlockall] = __NR_munlockall,
[X86__NR_sched_setparam] = __NR_sched_setparam,
[X86__NR_sched_getparam] = __NR_sched_getparam,
[X86__NR_sched_setscheduler] = __NR_sched_setscheduler,
[X86__NR_sched_getscheduler] = __NR_sched_getscheduler,
[X86__NR_sched_yield] = __NR_sched_yield,
[X86__NR_sched_get_priority_max] = __NR_sched_get_priority_max,
[X86__NR_sched_get_priority_min] = __NR_sched_get_priority_min,
[X86__NR_sched_rr_get_interval] = __NR_sched_rr_get_interval,
[X86__NR_nanosleep] = __NR_nanosleep,
[X86__NR_mremap] = __NR_mremap,
[X86__NR_setresuid] = __NR_setresuid,
[X86__NR_getresuid] = __NR_getresuid,
[X86__NR_vm86] = ARCH_SYS_UNSUPP,
[X86__NR_query_module] = __NR_query_module,
[X86__NR_poll] = __NR_poll,
[X86__NR_nfsservctl] = __NR_nfsservctl,
[X86__NR_setresgid] = __NR_setresgid,
[X86__NR_getresgid] = __NR_getresgid,
[X86__NR_prctl] = __NR_prctl,
[X86__NR_rt_sigreturn] = __NR_rt_sigreturn,
[X86__NR_rt_sigaction] = __NR_rt_sigaction,
[X86__NR_rt_sigprocmask] = __NR_rt_sigprocmask,
[X86__NR_rt_sigpending] = __NR_rt_sigpending,
[X86__NR_rt_sigtimedwait] = __NR_rt_sigtimedwait,
[X86__NR_rt_sigqueueinfo] = __NR_rt_sigqueueinfo,
[X86__NR_rt_sigsuspend] = __NR_rt_sigsuspend,
[X86__NR_pread64] = __NR_pread64,
[X86__NR_pwrite64] = __NR_pwrite64,
[X86__NR_chown] = __NR_chown,
[X86__NR_getcwd] = __NR_getcwd,
[X86__NR_capget] = __NR_capget,
[X86__NR_capset] = __NR_capset,
[X86__NR_sigaltstack] = __NR_sigaltstack,
[X86__NR_sendfile] = __NR_sendfile,
[X86__NR_getpmsg] = __NR_getpmsg,
[X86__NR_putpmsg] = __NR_putpmsg,
[X86__NR_vfork] = __NR_vfork,
[X86__NR_ugetrlimit] = __NR_getrlimit,
[X86__NR_mmap2] = X86_SYS_mmap2,
[X86__NR_truncate64] = __NR_truncate,
[X86__NR_ftruncate64] = __NR_ftruncate,
[X86__NR_stat64] = __NR_stat,
[X86__NR_lstat64] = __NR_lstat,
[X86__NR_fstat64] = __NR_fstat,
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
[X86__NR_chown32] = __NR_chown,
[X86__NR_setuid32] = __NR_setuid,
[X86__NR_setgid32] = __NR_setgid,
[X86__NR_setfsuid32] = __NR_setfsuid,
[X86__NR_setfsgid32] = __NR_setfsgid,
[X86__NR_pivot_root] = ARCH_SYS_DEFAULT_LE0,
[X86__NR_mincore] = __NR_mincore,
[X86__NR_madvise] = __NR_madvise,
[X86__NR_getdents64] = __NR_getdents64,
[X86__NR_fcntl64] = __NR_fcntl,
[X86__NR_gettid] = __NR_gettid,
[X86__NR_readahead] = __NR_readahead,
[X86__NR_setxattr] = __NR_setxattr,
[X86__NR_lsetxattr] = __NR_lsetxattr,
[X86__NR_fsetxattr] = __NR_fsetxattr,
[X86__NR_getxattr] = __NR_getxattr,
[X86__NR_lgetxattr] = __NR_lgetxattr,
[X86__NR_fgetxattr] = __NR_fgetxattr,
[X86__NR_listxattr] = __NR_listxattr,
[X86__NR_llistxattr] = __NR_llistxattr,
[X86__NR_flistxattr] = __NR_flistxattr,
[X86__NR_removexattr] = __NR_removexattr,
[X86__NR_lremovexattr] = __NR_lremovexattr,
[X86__NR_fremovexattr] = __NR_fremovexattr,
[X86__NR_tkill] = __NR_tkill,
[X86__NR_sendfile64] = __NR_sendfile,
[X86__NR_futex] = __NR_futex,
[X86__NR_sched_setaffinity] = __NR_sched_setaffinity,
[X86__NR_sched_getaffinity] = __NR_sched_getaffinity,
[X86__NR_set_thread_area] = __NR_set_thread_area,
[X86__NR_get_thread_area] = __NR_get_thread_area,
[X86__NR_io_setup] = __NR_io_setup,
[X86__NR_io_destroy] = __NR_io_destroy,
[X86__NR_io_getevents] = __NR_io_getevents,
[X86__NR_io_submit] = __NR_io_submit,
[X86__NR_io_cancel] = __NR_io_cancel,
[X86__NR_fadvise64] = __NR_fadvise64,
[X86__NR_exit_group] = __NR_exit_group,
[X86__NR_lookup_dcookie] = __NR_lookup_dcookie,
[X86__NR_epoll_create] = __NR_epoll_create,
[X86__NR_epoll_ctl] = __NR_epoll_ctl,
[X86__NR_epoll_wait] = __NR_epoll_wait,
[X86__NR_remap_file_pages] = __NR_remap_file_pages,
[X86__NR_set_tid_address] = __NR_set_tid_address,
[X86__NR_timer_create] = __NR_timer_create,
[X86__NR_timer_settime] = __NR_timer_settime,
[X86__NR_timer_gettime] = __NR_timer_gettime,
[X86__NR_timer_getoverrun] = __NR_timer_getoverrun,
[X86__NR_timer_delete] = __NR_timer_delete,
[X86__NR_clock_settime] = __NR_clock_settime,
[X86__NR_clock_gettime] = __NR_clock_gettime,
[X86__NR_clock_getres] = __NR_clock_getres,
[X86__NR_clock_nanosleep] = __NR_clock_nanosleep,
[X86__NR_statfs64] = __NR_statfs,
[X86__NR_fstatfs64] = __NR_fstatfs,
[X86__NR_tgkill] = __NR_tgkill,
[X86__NR_utimes] = __NR_utimes,
[X86__NR_fadvise64_64] = __NR_fadvise64,
[X86__NR_vserver] = __NR_vserver,
[X86__NR_mbind] = __NR_mbind,
[X86__NR_get_mempolicy] = __NR_get_mempolicy,
[X86__NR_set_mempolicy] = __NR_set_mempolicy,
[X86__NR_mq_open] = __NR_mq_open,
[X86__NR_mq_unlink] = __NR_mq_unlink,
[X86__NR_mq_timedsend] = __NR_mq_timedsend,
[X86__NR_mq_timedreceive] = __NR_mq_timedreceive,
[X86__NR_mq_notify] = __NR_mq_notify,
[X86__NR_mq_getsetattr] = __NR_mq_getsetattr,
[X86__NR_kexec_load] = __NR_kexec_load,
[X86__NR_waitid] = __NR_waitid,
[X86__NR_add_key] = __NR_add_key,
[X86__NR_request_key] = __NR_request_key,
[X86__NR_keyctl] = __NR_keyctl,
[X86__NR_ioprio_set] = __NR_ioprio_set,
[X86__NR_ioprio_get] = __NR_ioprio_get,
[X86__NR_inotify_init] = __NR_inotify_init,
[X86__NR_inotify_add_watch] = __NR_inotify_add_watch,
[X86__NR_inotify_rm_watch] = __NR_inotify_rm_watch,
[X86__NR_migrate_pages] = __NR_migrate_pages,
[X86__NR_openat] = __NR_openat,
[X86__NR_mkdirat] = __NR_mkdirat,
[X86__NR_mknodat] = __NR_mknodat,
[X86__NR_fchownat] = __NR_fchownat,
[X86__NR_futimesat] = __NR_futimesat,
[X86__NR_fstatat64] = __NR_newfstatat,
[X86__NR_unlinkat] = __NR_unlinkat,
[X86__NR_renameat] = __NR_renameat,
[X86__NR_linkat] = __NR_linkat,
[X86__NR_symlinkat] = __NR_symlinkat,
[X86__NR_readlinkat] = __NR_readlinkat,
[X86__NR_fchmodat] = __NR_fchmodat,
[X86__NR_faccessat] = __NR_faccessat,
[X86__NR_pselect6] = __NR_pselect6,
[X86__NR_ppoll] = __NR_ppoll,
[X86__NR_unshare] = __NR_unshare,
[X86__NR_set_robust_list] = __NR_set_robust_list,
[X86__NR_get_robust_list] = __NR_get_robust_list,
[X86__NR_splice] = __NR_splice,
[X86__NR_sync_file_range] = __NR_sync_file_range,
[X86__NR_tee] = __NR_tee,
[X86__NR_vmsplice] = __NR_vmsplice,
[X86__NR_move_pages] = __NR_move_pages,
[X86__NR_getcpu] = __NR_getcpu,
[X86__NR_epoll_pwait] = __NR_epoll_pwait,
[X86__NR_utimensat] = __NR_utimensat,
[X86__NR_signalfd] = __NR_signalfd,
[X86__NR_timerfd_create] = __NR_timerfd_create,
[X86__NR_eventfd] = __NR_eventfd,
[X86__NR_fallocate] = __NR_fallocate,
[X86__NR_timerfd_settime] = __NR_timerfd_settime,
[X86__NR_timerfd_gettime] = __NR_timerfd_gettime,
[X86__NR_signalfd4] = __NR_signalfd4,
[X86__NR_eventfd2] = __NR_eventfd2,
[X86__NR_epoll_create1] = __NR_epoll_create1,
[X86__NR_dup3] = __NR_dup3,
[X86__NR_pipe2] = __NR_pipe2,
[X86__NR_inotify_init1] = __NR_inotify_init1,
[X86__NR_preadv] = __NR_preadv,
[X86__NR_pwritev] = __NR_pwritev,
[X86__NR_rt_tgsigqueueinfo] = __NR_rt_tgsigqueueinfo,
[X86__NR_perf_event_open] = __NR_perf_event_open,
[X86__NR_recvmmsg] = __NR_recvmmsg,
[X86__NR_fanotify_init] = __NR_fanotify_init,
[X86__NR_fanotify_mark] = __NR_fanotify_mark,
[X86__NR_prlimit64] = __NR_prlimit64,
[X86__NR_name_to_handle_at] = __NR_name_to_handle_at,
[X86__NR_open_by_handle_at] = __NR_open_by_handle_at,
[X86__NR_clock_adjtime] = __NR_clock_adjtime,
[X86__NR_syncfs] = __NR_syncfs,
[X86__NR_sendmmsg] = __NR_sendmmsg,
[X86__NR_setns] = __NR_setns,
[X86__NR_process_vm_readv] = __NR_process_vm_readv,
[X86__NR_process_vm_writev] = __NR_process_vm_writev,
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
