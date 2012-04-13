#ifndef _RT_X86_UNISTD_32_H
#define _RT_X86_UNISTD_32_H

/*
 * This file contains the system call numbers.
 */

#define X86__NR_restart_syscall      0
#define X86__NR_exit		  1
#define X86__NR_fork		  2
#define X86__NR_read		  3
#define X86__NR_write		  4
#define X86__NR_open		  5
#define X86__NR_close		  6
#define X86__NR_waitpid		  7
#define X86__NR_creat		  8
#define X86__NR_link		  9
#define X86__NR_unlink		 10
#define X86__NR_execve		 11
#define X86__NR_chdir		 12
#define X86__NR_time		 13
#define X86__NR_mknod		 14
#define X86__NR_chmod		 15
#define X86__NR_lchown		 16
#define X86__NR_break		 17
#define X86__NR_oldstat		 18
#define X86__NR_lseek		 19
#define X86__NR_getpid		 20
#define X86__NR_mount		 21
#define X86__NR_umount		 22
#define X86__NR_setuid		 23
#define X86__NR_getuid		 24
#define X86__NR_stime		 25
#define X86__NR_ptrace		 26
#define X86__NR_alarm		 27
#define X86__NR_oldfstat	 28
#define X86__NR_pause		 29
#define X86__NR_utime		 30
#define X86__NR_stty		 31
#define X86__NR_gtty		 32
#define X86__NR_access		 33
#define X86__NR_nice		 34
#define X86__NR_ftime		 35
#define X86__NR_sync		 36
#define X86__NR_kill		 37
#define X86__NR_rename		 38
#define X86__NR_mkdir		 39
#define X86__NR_rmdir		 40
#define X86__NR_dup		 41
#define X86__NR_pipe		 42
#define X86__NR_times		 43
#define X86__NR_prof		 44
#define X86__NR_brk		 45
#define X86__NR_setgid		 46
#define X86__NR_getgid		 47
#define X86__NR_signal		 48
#define X86__NR_geteuid		 49
#define X86__NR_getegid		 50
#define X86__NR_acct		 51
#define X86__NR_umount2		 52
#define X86__NR_lock		 53
#define X86__NR_ioctl		 54
#define X86__NR_fcntl		 55
#define X86__NR_mpx		 56
#define X86__NR_setpgid		 57
#define X86__NR_ulimit		 58
#define X86__NR_oldolduname	 59
#define X86__NR_umask		 60
#define X86__NR_chroot		 61
#define X86__NR_ustat		 62
#define X86__NR_dup2		 63
#define X86__NR_getppid		 64
#define X86__NR_getpgrp		 65
#define X86__NR_setsid		 66
#define X86__NR_sigaction		 67
#define X86__NR_sgetmask		 68
#define X86__NR_ssetmask		 69
#define X86__NR_setreuid		 70
#define X86__NR_setregid		 71
#define X86__NR_sigsuspend		 72
#define X86__NR_sigpending		 73
#define X86__NR_sethostname	 74
#define X86__NR_setrlimit		 75
#define X86__NR_getrlimit		 76   /* Back compatible 2Gig limited rlimit */
#define X86__NR_getrusage		 77
#define X86__NR_gettimeofday	 78
#define X86__NR_settimeofday	 79
#define X86__NR_getgroups		 80
#define X86__NR_setgroups		 81
#define X86__NR_select		 82
#define X86__NR_symlink		 83
#define X86__NR_oldlstat		 84
#define X86__NR_readlink		 85
#define X86__NR_uselib		 86
#define X86__NR_swapon		 87
#define X86__NR_reboot		 88
#define X86__NR_readdir		 89
#define X86__NR_mmap		 90
#define X86__NR_munmap		 91
#define X86__NR_truncate		 92
#define X86__NR_ftruncate		 93
#define X86__NR_fchmod		 94
#define X86__NR_fchown		 95
#define X86__NR_getpriority	 96
#define X86__NR_setpriority	 97
#define X86__NR_profil		 98
#define X86__NR_statfs		 99
#define X86__NR_fstatfs		100
#define X86__NR_ioperm		101
#define X86__NR_socketcall		102
#define X86__NR_syslog		103
#define X86__NR_setitimer		104
#define X86__NR_getitimer		105
#define X86__NR_stat		106
#define X86__NR_lstat		107
#define X86__NR_fstat		108
#define X86__NR_olduname		109
#define X86__NR_iopl		110
#define X86__NR_vhangup		111
#define X86__NR_idle		112
#define X86__NR_vm86old		113
#define X86__NR_wait4		114
#define X86__NR_swapoff		115
#define X86__NR_sysinfo		116
#define X86__NR_ipc		117
#define X86__NR_fsync		118
#define X86__NR_sigreturn		119
#define X86__NR_clone		120
#define X86__NR_setdomainname	121
#define X86__NR_uname		122
#define X86__NR_modify_ldt		123
#define X86__NR_adjtimex		124
#define X86__NR_mprotect		125
#define X86__NR_sigprocmask	126
#define X86__NR_create_module	127
#define X86__NR_init_module	128
#define X86__NR_delete_module	129
#define X86__NR_get_kernel_syms	130
#define X86__NR_quotactl		131
#define X86__NR_getpgid		132
#define X86__NR_fchdir		133
#define X86__NR_bdflush		134
#define X86__NR_sysfs		135
#define X86__NR_personality	136
#define X86__NR_afs_syscall	137 /* Syscall for Andrew File System */
#define X86__NR_setfsuid		138
#define X86__NR_setfsgid		139
#define X86__NR__llseek		140
#define X86__NR_getdents		141
#define X86__NR_newselect		142
#define X86__NR_flock		143
#define X86__NR_msync		144
#define X86__NR_readv		145
#define X86__NR_writev		146
#define X86__NR_getsid		147
#define X86__NR_fdatasync		148
#define X86__NR__sysctl		149
#define X86__NR_mlock		150
#define X86__NR_munlock		151
#define X86__NR_mlockall		152
#define X86__NR_munlockall		153
#define X86__NR_sched_setparam		154
#define X86__NR_sched_getparam		155
#define X86__NR_sched_setscheduler		156
#define X86__NR_sched_getscheduler		157
#define X86__NR_sched_yield		158
#define X86__NR_sched_get_priority_max	159
#define X86__NR_sched_get_priority_min	160
#define X86__NR_sched_rr_get_interval	161
#define X86__NR_nanosleep		162
#define X86__NR_mremap		163
#define X86__NR_setresuid		164
#define X86__NR_getresuid		165
#define X86__NR_vm86		166
#define X86__NR_query_module	167
#define X86__NR_poll		168
#define X86__NR_nfsservctl		169
#define X86__NR_setresgid		170
#define X86__NR_getresgid		171
#define X86__NR_prctl              172
#define X86__NR_rt_sigreturn	173
#define X86__NR_rt_sigaction	174
#define X86__NR_rt_sigprocmask	175
#define X86__NR_rt_sigpending	176
#define X86__NR_rt_sigtimedwait	177
#define X86__NR_rt_sigqueueinfo	178
#define X86__NR_rt_sigsuspend	179
#define X86__NR_pread64		180
#define X86__NR_pwrite64		181
#define X86__NR_chown		182
#define X86__NR_getcwd		183
#define X86__NR_capget		184
#define X86__NR_capset		185
#define X86__NR_sigaltstack	186
#define X86__NR_sendfile		187
#define X86__NR_getpmsg		188	/* some people actually want streams */
#define X86__NR_putpmsg		189	/* some people actually want streams */
#define X86__NR_vfork		190
#define X86__NR_ugetrlimit		191	/* SuS compliant getrlimit */
#define X86__NR_mmap2		192
#define X86__NR_truncate64		193
#define X86__NR_ftruncate64	194
#define X86__NR_stat64		195
#define X86__NR_lstat64		196
#define X86__NR_fstat64		197
#define X86__NR_lchown32		198
#define X86__NR_getuid32		199
#define X86__NR_getgid32		200
#define X86__NR_geteuid32		201
#define X86__NR_getegid32		202
#define X86__NR_setreuid32		203
#define X86__NR_setregid32		204
#define X86__NR_getgroups32	205
#define X86__NR_setgroups32	206
#define X86__NR_fchown32		207
#define X86__NR_setresuid32	208
#define X86__NR_getresuid32	209
#define X86__NR_setresgid32	210
#define X86__NR_getresgid32	211
#define X86__NR_chown32		212
#define X86__NR_setuid32		213
#define X86__NR_setgid32		214
#define X86__NR_setfsuid32		215
#define X86__NR_setfsgid32		216
#define X86__NR_pivot_root		217
#define X86__NR_mincore		218
#define X86__NR_madvise		219
#define X86__NR_madvise1		219	/* delete when C lib stub is removed */
#define X86__NR_getdents64		220
#define X86__NR_fcntl64		221
/* 223 is unused */
#define X86__NR_gettid		224
#define X86__NR_readahead		225
#define X86__NR_setxattr		226
#define X86__NR_lsetxattr		227
#define X86__NR_fsetxattr		228
#define X86__NR_getxattr		229
#define X86__NR_lgetxattr		230
#define X86__NR_fgetxattr		231
#define X86__NR_listxattr		232
#define X86__NR_llistxattr		233
#define X86__NR_flistxattr		234
#define X86__NR_removexattr	235
#define X86__NR_lremovexattr	236
#define X86__NR_fremovexattr	237
#define X86__NR_tkill		238
#define X86__NR_sendfile64		239
#define X86__NR_futex		240
#define X86__NR_sched_setaffinity	241
#define X86__NR_sched_getaffinity	242
#define X86__NR_set_thread_area	243
#define X86__NR_get_thread_area	244
#define X86__NR_io_setup		245
#define X86__NR_io_destroy		246
#define X86__NR_io_getevents	247
#define X86__NR_io_submit		248
#define X86__NR_io_cancel		249
#define X86__NR_fadvise64		250
/* 251 is available for reuse (was briefly sys_set_zone_reclaim) */
#define X86__NR_exit_group		252
#define X86__NR_lookup_dcookie	253
#define X86__NR_epoll_create	254
#define X86__NR_epoll_ctl		255
#define X86__NR_epoll_wait		256
#define X86__NR_remap_file_pages	257
#define X86__NR_set_tid_address	258
#define X86__NR_timer_create	259
#define X86__NR_timer_settime	(X86__NR_timer_create+1)
#define X86__NR_timer_gettime	(X86__NR_timer_create+2)
#define X86__NR_timer_getoverrun	(X86__NR_timer_create+3)
#define X86__NR_timer_delete	(X86__NR_timer_create+4)
#define X86__NR_clock_settime	(X86__NR_timer_create+5)
#define X86__NR_clock_gettime	(X86__NR_timer_create+6)
#define X86__NR_clock_getres	(X86__NR_timer_create+7)
#define X86__NR_clock_nanosleep	(X86__NR_timer_create+8)
#define X86__NR_statfs64		268
#define X86__NR_fstatfs64		269
#define X86__NR_tgkill		270
#define X86__NR_utimes		271
#define X86__NR_fadvise64_64	272
#define X86__NR_vserver		273
#define X86__NR_mbind		274
#define X86__NR_get_mempolicy	275
#define X86__NR_set_mempolicy	276
#define X86__NR_mq_open 		277
#define X86__NR_mq_unlink		(X86__NR_mq_open+1)
#define X86__NR_mq_timedsend	(X86__NR_mq_open+2)
#define X86__NR_mq_timedreceive	(X86__NR_mq_open+3)
#define X86__NR_mq_notify		(X86__NR_mq_open+4)
#define X86__NR_mq_getsetattr	(X86__NR_mq_open+5)
#define X86__NR_kexec_load		283
#define X86__NR_waitid		284
/* #define X86__NR_sys_setaltroot	285 */
#define X86__NR_add_key		286
#define X86__NR_request_key	287
#define X86__NR_keyctl		288
#define X86__NR_ioprio_set		289
#define X86__NR_ioprio_get		290
#define X86__NR_inotify_init	291
#define X86__NR_inotify_add_watch	292
#define X86__NR_inotify_rm_watch	293
#define X86__NR_migrate_pages	294
#define X86__NR_openat		295
#define X86__NR_mkdirat		296
#define X86__NR_mknodat		297
#define X86__NR_fchownat		298
#define X86__NR_futimesat		299
#define X86__NR_fstatat64		300
#define X86__NR_unlinkat		301
#define X86__NR_renameat		302
#define X86__NR_linkat		303
#define X86__NR_symlinkat		304
#define X86__NR_readlinkat		305
#define X86__NR_fchmodat		306
#define X86__NR_faccessat		307
#define X86__NR_pselect6		308
#define X86__NR_ppoll		309
#define X86__NR_unshare		310
#define X86__NR_set_robust_list	311
#define X86__NR_get_robust_list	312
#define X86__NR_splice		313
#define X86__NR_sync_file_range	314
#define X86__NR_tee		315
#define X86__NR_vmsplice		316
#define X86__NR_move_pages		317
#define X86__NR_getcpu		318
#define X86__NR_epoll_pwait	319
#define X86__NR_utimensat		320
#define X86__NR_signalfd		321
#define X86__NR_timerfd_create	322
#define X86__NR_eventfd		323
#define X86__NR_fallocate		324
#define X86__NR_timerfd_settime	325
#define X86__NR_timerfd_gettime	326
#define X86__NR_signalfd4		327
#define X86__NR_eventfd2		328
#define X86__NR_epoll_create1	329
#define X86__NR_dup3		330
#define X86__NR_pipe2		331
#define X86__NR_inotify_init1	332
#define X86__NR_preadv		333
#define X86__NR_pwritev		334
#define X86__NR_rt_tgsigqueueinfo	335
#define X86__NR_perf_event_open	336
#define X86__NR_recvmmsg		337
#define X86__NR_fanotify_init	338
#define X86__NR_fanotify_mark	339
#define X86__NR_prlimit64		340
#define X86__NR_name_to_handle_at	341
#define X86__NR_open_by_handle_at  342
#define X86__NR_clock_adjtime	343
#define X86__NR_syncfs             344
#define X86__NR_sendmmsg		345
#define X86__NR_setns		346
#define X86__NR_process_vm_readv	347
#define X86__NR_process_vm_writev	348

#ifndef __SYSCALL
#define __SYSCALL(x)
#endif

__SYSCALL(X86__NR_restart_syscall)
__SYSCALL(X86__NR_exit)
__SYSCALL(X86__NR_fork)
__SYSCALL(X86__NR_read)
__SYSCALL(X86__NR_write)
__SYSCALL(X86__NR_open)
__SYSCALL(X86__NR_close)
__SYSCALL(X86__NR_waitpid)
__SYSCALL(X86__NR_creat)
__SYSCALL(X86__NR_link)
__SYSCALL(X86__NR_unlink)
__SYSCALL(X86__NR_execve)
__SYSCALL(X86__NR_chdir)
__SYSCALL(X86__NR_time)
__SYSCALL(X86__NR_mknod)
__SYSCALL(X86__NR_chmod)
__SYSCALL(X86__NR_lchown)
__SYSCALL(X86__NR_break)
__SYSCALL(X86__NR_oldstat)
__SYSCALL(X86__NR_lseek)
__SYSCALL(X86__NR_getpid)
__SYSCALL(X86__NR_mount)
__SYSCALL(X86__NR_umount)
__SYSCALL(X86__NR_setuid)
__SYSCALL(X86__NR_getuid)
__SYSCALL(X86__NR_stime)
__SYSCALL(X86__NR_ptrace)
__SYSCALL(X86__NR_alarm)
__SYSCALL(X86__NR_oldfstat)
__SYSCALL(X86__NR_pause)
__SYSCALL(X86__NR_utime)
__SYSCALL(X86__NR_stty)
__SYSCALL(X86__NR_gtty)
__SYSCALL(X86__NR_access)
__SYSCALL(X86__NR_nice)
__SYSCALL(X86__NR_ftime)
__SYSCALL(X86__NR_sync)
__SYSCALL(X86__NR_kill)
__SYSCALL(X86__NR_rename)
__SYSCALL(X86__NR_mkdir)
__SYSCALL(X86__NR_rmdir)
__SYSCALL(X86__NR_dup)
__SYSCALL(X86__NR_pipe)
__SYSCALL(X86__NR_times)
__SYSCALL(X86__NR_prof)
__SYSCALL(X86__NR_brk)
__SYSCALL(X86__NR_setgid)
__SYSCALL(X86__NR_getgid)
__SYSCALL(X86__NR_signal)
__SYSCALL(X86__NR_geteuid)
__SYSCALL(X86__NR_getegid)
__SYSCALL(X86__NR_acct)
__SYSCALL(X86__NR_umount2)
__SYSCALL(X86__NR_lock)
__SYSCALL(X86__NR_ioctl)
__SYSCALL(X86__NR_fcntl)
__SYSCALL(X86__NR_mpx)
__SYSCALL(X86__NR_setpgid)
__SYSCALL(X86__NR_ulimit)
__SYSCALL(X86__NR_oldolduname)
__SYSCALL(X86__NR_umask)
__SYSCALL(X86__NR_chroot)
__SYSCALL(X86__NR_ustat)
__SYSCALL(X86__NR_dup2)
__SYSCALL(X86__NR_getppid)
__SYSCALL(X86__NR_getpgrp)
__SYSCALL(X86__NR_setsid)
__SYSCALL(X86__NR_sigaction)
__SYSCALL(X86__NR_sgetmask)
__SYSCALL(X86__NR_ssetmask)
__SYSCALL(X86__NR_setreuid)
__SYSCALL(X86__NR_setregid)
__SYSCALL(X86__NR_sigsuspend)
__SYSCALL(X86__NR_sigpending)
__SYSCALL(X86__NR_sethostname)
__SYSCALL(X86__NR_setrlimit)
__SYSCALL(X86__NR_getrlimit)
__SYSCALL(X86__NR_getrusage)
__SYSCALL(X86__NR_gettimeofday)
__SYSCALL(X86__NR_settimeofday)
__SYSCALL(X86__NR_getgroups)
__SYSCALL(X86__NR_setgroups)
__SYSCALL(X86__NR_select)
__SYSCALL(X86__NR_symlink)
__SYSCALL(X86__NR_oldlstat)
__SYSCALL(X86__NR_readlink)
__SYSCALL(X86__NR_uselib)
__SYSCALL(X86__NR_swapon)
__SYSCALL(X86__NR_reboot)
__SYSCALL(X86__NR_readdir)
__SYSCALL(X86__NR_mmap)
__SYSCALL(X86__NR_munmap)
__SYSCALL(X86__NR_truncate)
__SYSCALL(X86__NR_ftruncate)
__SYSCALL(X86__NR_fchmod)
__SYSCALL(X86__NR_fchown)
__SYSCALL(X86__NR_getpriority)
__SYSCALL(X86__NR_setpriority)
__SYSCALL(X86__NR_profil)
__SYSCALL(X86__NR_statfs)
__SYSCALL(X86__NR_fstatfs)
__SYSCALL(X86__NR_ioperm)
__SYSCALL(X86__NR_socketcall)
__SYSCALL(X86__NR_syslog)
__SYSCALL(X86__NR_setitimer)
__SYSCALL(X86__NR_getitimer)
__SYSCALL(X86__NR_stat)
__SYSCALL(X86__NR_lstat)
__SYSCALL(X86__NR_fstat)
__SYSCALL(X86__NR_olduname)
__SYSCALL(X86__NR_iopl)
__SYSCALL(X86__NR_vhangup)
__SYSCALL(X86__NR_idle)
__SYSCALL(X86__NR_vm86old)
__SYSCALL(X86__NR_wait4)
__SYSCALL(X86__NR_swapoff)
__SYSCALL(X86__NR_sysinfo)
__SYSCALL(X86__NR_ipc)
__SYSCALL(X86__NR_fsync)
__SYSCALL(X86__NR_sigreturn)
__SYSCALL(X86__NR_clone)
__SYSCALL(X86__NR_setdomainname)
__SYSCALL(X86__NR_uname)
__SYSCALL(X86__NR_modify_ldt)
__SYSCALL(X86__NR_adjtimex)
__SYSCALL(X86__NR_mprotect)
__SYSCALL(X86__NR_sigprocmask)
__SYSCALL(X86__NR_create_module)
__SYSCALL(X86__NR_init_module)
__SYSCALL(X86__NR_delete_module)
__SYSCALL(X86__NR_get_kernel_syms)
__SYSCALL(X86__NR_quotactl)
__SYSCALL(X86__NR_getpgid)
__SYSCALL(X86__NR_fchdir)
__SYSCALL(X86__NR_bdflush)
__SYSCALL(X86__NR_sysfs)
__SYSCALL(X86__NR_personality)
__SYSCALL(X86__NR_afs_syscall)
__SYSCALL(X86__NR_setfsuid)
__SYSCALL(X86__NR_setfsgid)
__SYSCALL(X86__NR__llseek)
__SYSCALL(X86__NR_getdents)
__SYSCALL(X86__NR_newselect)
__SYSCALL(X86__NR_flock)
__SYSCALL(X86__NR_msync)
__SYSCALL(X86__NR_readv)
__SYSCALL(X86__NR_writev)
__SYSCALL(X86__NR_getsid)
__SYSCALL(X86__NR_fdatasync)
__SYSCALL(X86__NR__sysctl)
__SYSCALL(X86__NR_mlock)
__SYSCALL(X86__NR_munlock)
__SYSCALL(X86__NR_mlockall)
__SYSCALL(X86__NR_munlockall)
__SYSCALL(X86__NR_sched_setparam)
__SYSCALL(X86__NR_sched_getparam)
__SYSCALL(X86__NR_sched_setscheduler)
__SYSCALL(X86__NR_sched_getscheduler)
__SYSCALL(X86__NR_sched_yield)
__SYSCALL(X86__NR_sched_get_priority_max)
__SYSCALL(X86__NR_sched_get_priority_min)
__SYSCALL(X86__NR_sched_rr_get_interval)
__SYSCALL(X86__NR_nanosleep)
__SYSCALL(X86__NR_mremap)
__SYSCALL(X86__NR_setresuid)
__SYSCALL(X86__NR_getresuid)
__SYSCALL(X86__NR_vm86)
__SYSCALL(X86__NR_query_module)
__SYSCALL(X86__NR_poll)
__SYSCALL(X86__NR_nfsservctl)
__SYSCALL(X86__NR_setresgid)
__SYSCALL(X86__NR_getresgid)
__SYSCALL(X86__NR_prctl)
__SYSCALL(X86__NR_rt_sigreturn)
__SYSCALL(X86__NR_rt_sigaction)
__SYSCALL(X86__NR_rt_sigprocmask)
__SYSCALL(X86__NR_rt_sigpending)
__SYSCALL(X86__NR_rt_sigtimedwait)
__SYSCALL(X86__NR_rt_sigqueueinfo)
__SYSCALL(X86__NR_rt_sigsuspend)
__SYSCALL(X86__NR_pread64)
__SYSCALL(X86__NR_pwrite64)
__SYSCALL(X86__NR_chown)
__SYSCALL(X86__NR_getcwd)
__SYSCALL(X86__NR_capget)
__SYSCALL(X86__NR_capset)
__SYSCALL(X86__NR_sigaltstack)
__SYSCALL(X86__NR_sendfile)
__SYSCALL(X86__NR_getpmsg)
__SYSCALL(X86__NR_putpmsg)
__SYSCALL(X86__NR_vfork)
__SYSCALL(X86__NR_ugetrlimit)
__SYSCALL(X86__NR_mmap2)
__SYSCALL(X86__NR_truncate64)
__SYSCALL(X86__NR_ftruncate64)
__SYSCALL(X86__NR_stat64)
__SYSCALL(X86__NR_lstat64)
__SYSCALL(X86__NR_fstat64)
__SYSCALL(X86__NR_lchown32)
__SYSCALL(X86__NR_getuid32)
__SYSCALL(X86__NR_getgid32)
__SYSCALL(X86__NR_geteuid32)
__SYSCALL(X86__NR_getegid32)
__SYSCALL(X86__NR_setreuid32)
__SYSCALL(X86__NR_setregid32)
__SYSCALL(X86__NR_getgroups32)
__SYSCALL(X86__NR_setgroups32)
__SYSCALL(X86__NR_fchown32)
__SYSCALL(X86__NR_setresuid32)
__SYSCALL(X86__NR_getresuid32)
__SYSCALL(X86__NR_setresgid32)
__SYSCALL(X86__NR_getresgid32)
__SYSCALL(X86__NR_chown32)
__SYSCALL(X86__NR_setuid32)
__SYSCALL(X86__NR_setgid32)
__SYSCALL(X86__NR_setfsuid32)
__SYSCALL(X86__NR_setfsgid32)
__SYSCALL(X86__NR_pivot_root)
__SYSCALL(X86__NR_mincore)
__SYSCALL(X86__NR_madvise)
__SYSCALL(X86__NR_madvise1)	/* delete when C lib stub is removed */
__SYSCALL(X86__NR_getdents64)
__SYSCALL(X86__NR_fcntl64)
/* 223 is unused */
__SYSCALL(X86__NR_gettid)
__SYSCALL(X86__NR_readahead)
__SYSCALL(X86__NR_setxattr)
__SYSCALL(X86__NR_lsetxattr)
__SYSCALL(X86__NR_fsetxattr)
__SYSCALL(X86__NR_getxattr)
__SYSCALL(X86__NR_lgetxattr)
__SYSCALL(X86__NR_fgetxattr)
__SYSCALL(X86__NR_listxattr)
__SYSCALL(X86__NR_llistxattr)
__SYSCALL(X86__NR_flistxattr)
__SYSCALL(X86__NR_removexattr)
__SYSCALL(X86__NR_lremovexattr)
__SYSCALL(X86__NR_fremovexattr)
__SYSCALL(X86__NR_tkill)
__SYSCALL(X86__NR_sendfile64)
__SYSCALL(X86__NR_futex)
__SYSCALL(X86__NR_sched_setaffinity)
__SYSCALL(X86__NR_sched_getaffinity)
__SYSCALL(X86__NR_set_thread_area)
__SYSCALL(X86__NR_get_thread_area)
__SYSCALL(X86__NR_io_setup)
__SYSCALL(X86__NR_io_destroy)
__SYSCALL(X86__NR_io_getevents)
__SYSCALL(X86__NR_io_submit)
__SYSCALL(X86__NR_io_cancel)
__SYSCALL(X86__NR_fadvise64)
/* 251 is available for reuse (was briefly sys_set_zone_reclaim) */
__SYSCALL(X86__NR_exit_group)
__SYSCALL(X86__NR_lookup_dcookie)
__SYSCALL(X86__NR_epoll_create)
__SYSCALL(X86__NR_epoll_ctl)
__SYSCALL(X86__NR_epoll_wait)
__SYSCALL(X86__NR_remap_file_pages)
__SYSCALL(X86__NR_set_tid_address)
__SYSCALL(X86__NR_timer_create)
__SYSCALL(X86__NR_timer_settime)
__SYSCALL(X86__NR_timer_gettime)
__SYSCALL(X86__NR_timer_getoverrun)
__SYSCALL(X86__NR_timer_delete)
__SYSCALL(X86__NR_clock_settime)
__SYSCALL(X86__NR_clock_gettime)
__SYSCALL(X86__NR_clock_getres)
__SYSCALL(X86__NR_clock_nanosleep)
__SYSCALL(X86__NR_statfs64)
__SYSCALL(X86__NR_fstatfs64)
__SYSCALL(X86__NR_tgkill)
__SYSCALL(X86__NR_utimes)
__SYSCALL(X86__NR_fadvise64_64)
__SYSCALL(X86__NR_vserver)
__SYSCALL(X86__NR_mbind)
__SYSCALL(X86__NR_get_mempolicy)
__SYSCALL(X86__NR_set_mempolicy)
__SYSCALL(X86__NR_mq_open)
__SYSCALL(X86__NR_mq_unlink)
__SYSCALL(X86__NR_mq_timedsend)
__SYSCALL(X86__NR_mq_timedreceive)
__SYSCALL(X86__NR_mq_notify)
__SYSCALL(X86__NR_mq_getsetattr)
__SYSCALL(X86__NR_kexec_load)
__SYSCALL(X86__NR_waitid)
/* __SYSCALL(X86__NR_sys_setaltroot	285 */
__SYSCALL(X86__NR_add_key)
__SYSCALL(X86__NR_request_key)
__SYSCALL(X86__NR_keyctl)
__SYSCALL(X86__NR_ioprio_set)
__SYSCALL(X86__NR_ioprio_get)
__SYSCALL(X86__NR_inotify_init)
__SYSCALL(X86__NR_inotify_add_watch)
__SYSCALL(X86__NR_inotify_rm_watch)
__SYSCALL(X86__NR_migrate_pages)
__SYSCALL(X86__NR_openat)
__SYSCALL(X86__NR_mkdirat)
__SYSCALL(X86__NR_mknodat)
__SYSCALL(X86__NR_fchownat)
__SYSCALL(X86__NR_futimesat)
__SYSCALL(X86__NR_fstatat64)
__SYSCALL(X86__NR_unlinkat)
__SYSCALL(X86__NR_renameat)
__SYSCALL(X86__NR_linkat)
__SYSCALL(X86__NR_symlinkat)
__SYSCALL(X86__NR_readlinkat)
__SYSCALL(X86__NR_fchmodat)
__SYSCALL(X86__NR_faccessat)
__SYSCALL(X86__NR_pselect6)
__SYSCALL(X86__NR_ppoll)
__SYSCALL(X86__NR_unshare)
__SYSCALL(X86__NR_set_robust_list)
__SYSCALL(X86__NR_get_robust_list)
__SYSCALL(X86__NR_splice)
__SYSCALL(X86__NR_sync_file_range)
__SYSCALL(X86__NR_tee)
__SYSCALL(X86__NR_vmsplice)
__SYSCALL(X86__NR_move_pages)
__SYSCALL(X86__NR_getcpu)
__SYSCALL(X86__NR_epoll_pwait)
__SYSCALL(X86__NR_utimensat)
__SYSCALL(X86__NR_signalfd)
__SYSCALL(X86__NR_timerfd_create)
__SYSCALL(X86__NR_eventfd)
__SYSCALL(X86__NR_fallocate)
__SYSCALL(X86__NR_timerfd_settime)
__SYSCALL(X86__NR_timerfd_gettime)
__SYSCALL(X86__NR_signalfd4)
__SYSCALL(X86__NR_eventfd2)
__SYSCALL(X86__NR_epoll_create1)
__SYSCALL(X86__NR_dup3)
__SYSCALL(X86__NR_pipe2)
__SYSCALL(X86__NR_inotify_init1)
__SYSCALL(X86__NR_preadv)
__SYSCALL(X86__NR_pwritev)
__SYSCALL(X86__NR_rt_tgsigqueueinfo)
__SYSCALL(X86__NR_perf_event_open)
__SYSCALL(X86__NR_recvmmsg)
__SYSCALL(X86__NR_fanotify_init)
__SYSCALL(X86__NR_fanotify_mark)
__SYSCALL(X86__NR_prlimit64)
__SYSCALL(X86__NR_name_to_handle_at)
__SYSCALL(X86__NR_open_by_handle_at)
__SYSCALL(X86__NR_clock_adjtime)
__SYSCALL(X86__NR_syncfs)
__SYSCALL(X86__NR_sendmmsg)
__SYSCALL(X86__NR_setns)
__SYSCALL(X86__NR_process_vm_readv)
__SYSCALL(X86__NR_process_vm_writev)


#endif /* _RT_X86_UNISTD_32_H */
