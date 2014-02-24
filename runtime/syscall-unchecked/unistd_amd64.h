#ifndef _ASM_X86_UNISTD_64_H
#define _ASM_X86_UNISTD_64_H 1

#ifndef __SYSCALL
#error why are you including this if no __SYSCALL
#endif

#define __NR_read 0
#define __NR_write 1
#define __NR_open 2
#define __NR_close 3
#define __NR_stat 4
#define __NR_fstat 5
#define __NR_lstat 6
#define __NR_poll 7
#define __NR_lseek 8
#define __NR_mmap 9
#define __NR_mprotect 10
#define __NR_munmap 11
#define __NR_brk 12
#define __NR_rt_sigaction 13
#define __NR_rt_sigprocmask 14
#define __NR_rt_sigreturn 15
#define __NR_ioctl 16
#define __NR_pread64 17
#define __NR_pwrite64 18
#define __NR_readv 19
#define __NR_writev 20
#define __NR_access 21
#define __NR_pipe 22
#define __NR_select 23
#define __NR_sched_yield 24
#define __NR_mremap 25
#define __NR_msync 26
#define __NR_mincore 27
#define __NR_madvise 28
#define __NR_shmget 29
#define __NR_shmat 30
#define __NR_shmctl 31
#define __NR_dup 32
#define __NR_dup2 33
#define __NR_pause 34
#define __NR_nanosleep 35
#define __NR_getitimer 36
#define __NR_alarm 37
#define __NR_setitimer 38
#define __NR_getpid 39
#define __NR_sendfile 40
#define __NR_socket 41
#define __NR_connect 42
#define __NR_accept 43
#define __NR_sendto 44
#define __NR_recvfrom 45
#define __NR_sendmsg 46
#define __NR_recvmsg 47
#define __NR_shutdown 48
#define __NR_bind 49
#define __NR_listen 50
#define __NR_getsockname 51
#define __NR_getpeername 52
#define __NR_socketpair 53
#define __NR_setsockopt 54
#define __NR_getsockopt 55
#define __NR_clone 56
#define __NR_fork 57
#define __NR_vfork 58
#define __NR_execve 59
#define __NR_exit 60
#define __NR_wait4 61
#define __NR_kill 62
#define __NR_uname 63
#define __NR_semget 64
#define __NR_semop 65
#define __NR_semctl 66
#define __NR_shmdt 67
#define __NR_msgget 68
#define __NR_msgsnd 69
#define __NR_msgrcv 70
#define __NR_msgctl 71
#define __NR_fcntl 72
#define __NR_flock 73
#define __NR_fsync 74
#define __NR_fdatasync 75
#define __NR_truncate 76
#define __NR_ftruncate 77
#define __NR_getdents 78
#define __NR_getcwd 79
#define __NR_chdir 80
#define __NR_fchdir 81
#define __NR_rename 82
#define __NR_mkdir 83
#define __NR_rmdir 84
#define __NR_creat 85
#define __NR_link 86
#define __NR_unlink 87
#define __NR_symlink 88
#define __NR_readlink 89
#define __NR_chmod 90
#define __NR_fchmod 91
#define __NR_chown 92
#define __NR_fchown 93
#define __NR_lchown 94
#define __NR_umask 95
#define __NR_gettimeofday 96
#define __NR_getrlimit 97
#define __NR_getrusage 98
#define __NR_sysinfo 99
#define __NR_times 100
#define __NR_ptrace 101
#define __NR_getuid 102
#define __NR_syslog 103
#define __NR_getgid 104
#define __NR_setuid 105
#define __NR_setgid 106
#define __NR_geteuid 107
#define __NR_getegid 108
#define __NR_setpgid 109
#define __NR_getppid 110
#define __NR_getpgrp 111
#define __NR_setsid 112
#define __NR_setreuid 113
#define __NR_setregid 114
#define __NR_getgroups 115
#define __NR_setgroups 116
#define __NR_setresuid 117
#define __NR_getresuid 118
#define __NR_setresgid 119
#define __NR_getresgid 120
#define __NR_getpgid 121
#define __NR_setfsuid 122
#define __NR_setfsgid 123
#define __NR_getsid 124
#define __NR_capget 125
#define __NR_capset 126
#define __NR_rt_sigpending 127
#define __NR_rt_sigtimedwait 128
#define __NR_rt_sigqueueinfo 129
#define __NR_rt_sigsuspend 130
#define __NR_sigaltstack 131
#define __NR_utime 132
#define __NR_mknod 133
#define __NR_uselib 134
#define __NR_personality 135
#define __NR_ustat 136
#define __NR_statfs 137
#define __NR_fstatfs 138
#define __NR_sysfs 139
#define __NR_getpriority 140
#define __NR_setpriority 141
#define __NR_sched_setparam 142
#define __NR_sched_getparam 143
#define __NR_sched_setscheduler 144
#define __NR_sched_getscheduler 145
#define __NR_sched_get_priority_max 146
#define __NR_sched_get_priority_min 147
#define __NR_sched_rr_get_interval 148
#define __NR_mlock 149
#define __NR_munlock 150
#define __NR_mlockall 151
#define __NR_munlockall 152
#define __NR_vhangup 153
#define __NR_modify_ldt 154
#define __NR_pivot_root 155
#define __NR__sysctl 156
#define __NR_prctl 157
#define __NR_arch_prctl 158
#define __NR_adjtimex 159
#define __NR_setrlimit 160
#define __NR_chroot 161
#define __NR_sync 162
#define __NR_acct 163
#define __NR_settimeofday 164
#define __NR_mount 165
#define __NR_umount2 166
#define __NR_swapon 167
#define __NR_swapoff 168
#define __NR_reboot 169
#define __NR_sethostname 170
#define __NR_setdomainname 171
#define __NR_iopl 172
#define __NR_ioperm 173
#define __NR_create_module 174
#define __NR_init_module 175
#define __NR_delete_module 176
#define __NR_get_kernel_syms 177
#define __NR_query_module 178
#define __NR_quotactl 179
#define __NR_nfsservctl 180
#define __NR_getpmsg 181
#define __NR_putpmsg 182
#define __NR_afs_syscall 183
#define __NR_tuxcall 184
#define __NR_security 185
#define __NR_gettid 186
#define __NR_readahead 187
#define __NR_setxattr 188
#define __NR_lsetxattr 189
#define __NR_fsetxattr 190
#define __NR_getxattr 191
#define __NR_lgetxattr 192
#define __NR_fgetxattr 193
#define __NR_listxattr 194
#define __NR_llistxattr 195
#define __NR_flistxattr 196
#define __NR_removexattr 197
#define __NR_lremovexattr 198
#define __NR_fremovexattr 199
#define __NR_tkill 200
#define __NR_time 201
#define __NR_futex 202
#define __NR_sched_setaffinity 203
#define __NR_sched_getaffinity 204
#define __NR_set_thread_area 205
#define __NR_io_setup 206
#define __NR_io_destroy 207
#define __NR_io_getevents 208
#define __NR_io_submit 209
#define __NR_io_cancel 210
#define __NR_get_thread_area 211
#define __NR_lookup_dcookie 212
#define __NR_epoll_create 213
#define __NR_epoll_ctl_old 214
#define __NR_epoll_wait_old 215
#define __NR_remap_file_pages 216
#define __NR_getdents64 217
#define __NR_set_tid_address 218
#define __NR_restart_syscall 219
#define __NR_semtimedop 220
#define __NR_fadvise64 221
#define __NR_timer_create 222
#define __NR_timer_settime 223
#define __NR_timer_gettime 224
#define __NR_timer_getoverrun 225
#define __NR_timer_delete 226
#define __NR_clock_settime 227
#define __NR_clock_gettime 228
#define __NR_clock_getres 229
#define __NR_clock_nanosleep 230
#define __NR_exit_group 231
#define __NR_epoll_wait 232
#define __NR_epoll_ctl 233
#define __NR_tgkill 234
#define __NR_utimes 235
#define __NR_vserver 236
#define __NR_mbind 237
#define __NR_set_mempolicy 238
#define __NR_get_mempolicy 239
#define __NR_mq_open 240
#define __NR_mq_unlink 241
#define __NR_mq_timedsend 242
#define __NR_mq_timedreceive 243
#define __NR_mq_notify 244
#define __NR_mq_getsetattr 245
#define __NR_kexec_load 246
#define __NR_waitid 247
#define __NR_add_key 248
#define __NR_request_key 249
#define __NR_keyctl 250
#define __NR_ioprio_set 251
#define __NR_ioprio_get 252
#define __NR_inotify_init 253
#define __NR_inotify_add_watch 254
#define __NR_inotify_rm_watch 255
#define __NR_migrate_pages 256
#define __NR_openat 257
#define __NR_mkdirat 258
#define __NR_mknodat 259
#define __NR_fchownat 260
#define __NR_futimesat 261
#define __NR_newfstatat 262
#define __NR_unlinkat 263
#define __NR_renameat 264
#define __NR_linkat 265
#define __NR_symlinkat 266
#define __NR_readlinkat 267
#define __NR_fchmodat 268
#define __NR_faccessat 269
#define __NR_pselect6 270
#define __NR_ppoll 271
#define __NR_unshare 272
#define __NR_set_robust_list 273
#define __NR_get_robust_list 274
#define __NR_splice 275
#define __NR_tee 276
#define __NR_sync_file_range 277
#define __NR_vmsplice 278
#define __NR_move_pages 279
#define __NR_utimensat 280
#define __NR_epoll_pwait 281
#define __NR_signalfd 282
#define __NR_timerfd_create 283
#define __NR_eventfd 284
#define __NR_fallocate 285
#define __NR_timerfd_settime 286
#define __NR_timerfd_gettime 287
#define __NR_accept4 288
#define __NR_signalfd4 289
#define __NR_eventfd2 290
#define __NR_epoll_create1 291
#define __NR_dup3 292
#define __NR_pipe2 293
#define __NR_inotify_init1 294
#define __NR_preadv 295
#define __NR_pwritev 296
#define __NR_rt_tgsigqueueinfo 297
#define __NR_perf_event_open 298
#define __NR_recvmmsg 299
#define __NR_fanotify_init 300
#define __NR_fanotify_mark 301
#define __NR_prlimit64 302
#define __NR_name_to_handle_at 303
#define __NR_open_by_handle_at 304
#define __NR_clock_adjtime 305
#define __NR_syncfs 306
#define __NR_sendmmsg 307
#define __NR_setns 308
#define __NR_getcpu 309
#define __NR_process_vm_readv 310
#define __NR_process_vm_writev 311

__SYSCALL(__NR_read)
__SYSCALL(__NR_write)
__SYSCALL(__NR_open)
__SYSCALL(__NR_close)
__SYSCALL(__NR_stat)
__SYSCALL(__NR_fstat)
__SYSCALL(__NR_lstat)
__SYSCALL(__NR_poll)
__SYSCALL(__NR_lseek)
__SYSCALL(__NR_mmap)
__SYSCALL(__NR_mprotect)
__SYSCALL(__NR_munmap)
__SYSCALL(__NR_brk)
__SYSCALL(__NR_rt_sigaction)
__SYSCALL(__NR_rt_sigprocmask)
__SYSCALL(__NR_rt_sigreturn)
__SYSCALL(__NR_ioctl)
__SYSCALL(__NR_pread64)
__SYSCALL(__NR_pwrite64)
__SYSCALL(__NR_readv)
__SYSCALL(__NR_writev)
__SYSCALL(__NR_access)
__SYSCALL(__NR_pipe)
__SYSCALL(__NR_select)
__SYSCALL(__NR_sched_yield)
__SYSCALL(__NR_mremap)
__SYSCALL(__NR_msync)
__SYSCALL(__NR_mincore)
__SYSCALL(__NR_madvise)
__SYSCALL(__NR_shmget)
__SYSCALL(__NR_shmat)
__SYSCALL(__NR_shmctl)
__SYSCALL(__NR_dup)
__SYSCALL(__NR_dup2)
__SYSCALL(__NR_pause)
__SYSCALL(__NR_nanosleep)
__SYSCALL(__NR_getitimer)
__SYSCALL(__NR_alarm)
__SYSCALL(__NR_setitimer)
__SYSCALL(__NR_getpid)
__SYSCALL(__NR_sendfile)
__SYSCALL(__NR_socket)
__SYSCALL(__NR_connect)
__SYSCALL(__NR_accept)
__SYSCALL(__NR_sendto)
__SYSCALL(__NR_recvfrom)
__SYSCALL(__NR_sendmsg)
__SYSCALL(__NR_recvmsg)
__SYSCALL(__NR_shutdown)
__SYSCALL(__NR_bind)
__SYSCALL(__NR_listen)
__SYSCALL(__NR_getsockname)
__SYSCALL(__NR_getpeername)
__SYSCALL(__NR_socketpair)
__SYSCALL(__NR_setsockopt)
__SYSCALL(__NR_getsockopt)
__SYSCALL(__NR_clone)
__SYSCALL(__NR_fork)
__SYSCALL(__NR_vfork)
__SYSCALL(__NR_execve)
__SYSCALL(__NR_exit)
__SYSCALL(__NR_wait4)
__SYSCALL(__NR_kill)
__SYSCALL(__NR_uname)
__SYSCALL(__NR_semget)
__SYSCALL(__NR_semop)
__SYSCALL(__NR_semctl)
__SYSCALL(__NR_shmdt)
__SYSCALL(__NR_msgget)
__SYSCALL(__NR_msgsnd)
__SYSCALL(__NR_msgrcv)
__SYSCALL(__NR_msgctl)
__SYSCALL(__NR_fcntl)
__SYSCALL(__NR_flock)
__SYSCALL(__NR_fsync)
__SYSCALL(__NR_fdatasync)
__SYSCALL(__NR_truncate)
__SYSCALL(__NR_ftruncate)
__SYSCALL(__NR_getdents)
__SYSCALL(__NR_getcwd)
__SYSCALL(__NR_chdir)
__SYSCALL(__NR_fchdir)
__SYSCALL(__NR_rename)
__SYSCALL(__NR_mkdir)
__SYSCALL(__NR_rmdir)
__SYSCALL(__NR_creat)
__SYSCALL(__NR_link)
__SYSCALL(__NR_unlink)
__SYSCALL(__NR_symlink)
__SYSCALL(__NR_readlink)
__SYSCALL(__NR_chmod)
__SYSCALL(__NR_fchmod)
__SYSCALL(__NR_chown)
__SYSCALL(__NR_fchown)
__SYSCALL(__NR_lchown)
__SYSCALL(__NR_umask)
__SYSCALL(__NR_gettimeofday)
__SYSCALL(__NR_getrlimit)
__SYSCALL(__NR_getrusage)
__SYSCALL(__NR_sysinfo)
__SYSCALL(__NR_times)
__SYSCALL(__NR_ptrace)
__SYSCALL(__NR_getuid)
__SYSCALL(__NR_syslog)
__SYSCALL(__NR_getgid)
__SYSCALL(__NR_setuid)
__SYSCALL(__NR_setgid)
__SYSCALL(__NR_geteuid)
__SYSCALL(__NR_getegid)
__SYSCALL(__NR_setpgid)
__SYSCALL(__NR_getppid)
__SYSCALL(__NR_getpgrp)
__SYSCALL(__NR_setsid)
__SYSCALL(__NR_setreuid)
__SYSCALL(__NR_setregid)
__SYSCALL(__NR_getgroups)
__SYSCALL(__NR_setgroups)
__SYSCALL(__NR_setresuid)
__SYSCALL(__NR_getresuid)
__SYSCALL(__NR_setresgid)
__SYSCALL(__NR_getresgid)
__SYSCALL(__NR_getpgid)
__SYSCALL(__NR_setfsuid)
__SYSCALL(__NR_setfsgid)
__SYSCALL(__NR_getsid)
__SYSCALL(__NR_capget)
__SYSCALL(__NR_capset)
__SYSCALL(__NR_rt_sigpending)
__SYSCALL(__NR_rt_sigtimedwait)
__SYSCALL(__NR_rt_sigqueueinfo)
__SYSCALL(__NR_rt_sigsuspend)
__SYSCALL(__NR_sigaltstack)
__SYSCALL(__NR_utime)
__SYSCALL(__NR_mknod)
__SYSCALL(__NR_uselib)
__SYSCALL(__NR_personality)
__SYSCALL(__NR_ustat)
__SYSCALL(__NR_statfs)
__SYSCALL(__NR_fstatfs)
__SYSCALL(__NR_sysfs)
__SYSCALL(__NR_getpriority)
__SYSCALL(__NR_setpriority)
__SYSCALL(__NR_sched_setparam)
__SYSCALL(__NR_sched_getparam)
__SYSCALL(__NR_sched_setscheduler)
__SYSCALL(__NR_sched_getscheduler)
__SYSCALL(__NR_sched_get_priority_max)
__SYSCALL(__NR_sched_get_priority_min)
__SYSCALL(__NR_sched_rr_get_interval)
__SYSCALL(__NR_mlock)
__SYSCALL(__NR_munlock)
__SYSCALL(__NR_mlockall)
__SYSCALL(__NR_munlockall)
__SYSCALL(__NR_vhangup)
__SYSCALL(__NR_modify_ldt)
__SYSCALL(__NR_pivot_root)
__SYSCALL(__NR__sysctl)
__SYSCALL(__NR_prctl)
__SYSCALL(__NR_arch_prctl)
__SYSCALL(__NR_adjtimex)
__SYSCALL(__NR_setrlimit)
__SYSCALL(__NR_chroot)
__SYSCALL(__NR_sync)
__SYSCALL(__NR_acct)
__SYSCALL(__NR_settimeofday)
__SYSCALL(__NR_mount)
__SYSCALL(__NR_umount2)
__SYSCALL(__NR_swapon)
__SYSCALL(__NR_swapoff)
__SYSCALL(__NR_reboot)
__SYSCALL(__NR_sethostname)
__SYSCALL(__NR_setdomainname)
__SYSCALL(__NR_iopl)
__SYSCALL(__NR_ioperm)
__SYSCALL(__NR_create_module)
__SYSCALL(__NR_init_module)
__SYSCALL(__NR_delete_module)
__SYSCALL(__NR_get_kernel_syms)
__SYSCALL(__NR_query_module)
__SYSCALL(__NR_quotactl)
__SYSCALL(__NR_nfsservctl)
__SYSCALL(__NR_getpmsg)
__SYSCALL(__NR_putpmsg)
__SYSCALL(__NR_afs_syscall)
__SYSCALL(__NR_tuxcall)
__SYSCALL(__NR_security)
__SYSCALL(__NR_gettid)
__SYSCALL(__NR_readahead)
__SYSCALL(__NR_setxattr)
__SYSCALL(__NR_lsetxattr)
__SYSCALL(__NR_fsetxattr)
__SYSCALL(__NR_getxattr)
__SYSCALL(__NR_lgetxattr)
__SYSCALL(__NR_fgetxattr)
__SYSCALL(__NR_listxattr)
__SYSCALL(__NR_llistxattr)
__SYSCALL(__NR_flistxattr)
__SYSCALL(__NR_removexattr)
__SYSCALL(__NR_lremovexattr)
__SYSCALL(__NR_fremovexattr)
__SYSCALL(__NR_tkill)
__SYSCALL(__NR_time)
__SYSCALL(__NR_futex)
__SYSCALL(__NR_sched_setaffinity)
__SYSCALL(__NR_sched_getaffinity)
__SYSCALL(__NR_set_thread_area)
__SYSCALL(__NR_io_setup)
__SYSCALL(__NR_io_destroy)
__SYSCALL(__NR_io_getevents)
__SYSCALL(__NR_io_submit)
__SYSCALL(__NR_io_cancel)
__SYSCALL(__NR_get_thread_area)
__SYSCALL(__NR_lookup_dcookie)
__SYSCALL(__NR_epoll_create)
__SYSCALL(__NR_epoll_ctl_old)
__SYSCALL(__NR_epoll_wait_old)
__SYSCALL(__NR_remap_file_pages)
__SYSCALL(__NR_getdents64)
__SYSCALL(__NR_set_tid_address)
__SYSCALL(__NR_restart_syscall)
__SYSCALL(__NR_semtimedop)
__SYSCALL(__NR_fadvise64)
__SYSCALL(__NR_timer_create)
__SYSCALL(__NR_timer_settime)
__SYSCALL(__NR_timer_gettime)
__SYSCALL(__NR_timer_getoverrun)
__SYSCALL(__NR_timer_delete)
__SYSCALL(__NR_clock_settime)
__SYSCALL(__NR_clock_gettime)
__SYSCALL(__NR_clock_getres)
__SYSCALL(__NR_clock_nanosleep)
__SYSCALL(__NR_exit_group)
__SYSCALL(__NR_epoll_wait)
__SYSCALL(__NR_epoll_ctl)
__SYSCALL(__NR_tgkill)
__SYSCALL(__NR_utimes)
__SYSCALL(__NR_vserver)
__SYSCALL(__NR_mbind)
__SYSCALL(__NR_set_mempolicy)
__SYSCALL(__NR_get_mempolicy)
__SYSCALL(__NR_mq_open)
__SYSCALL(__NR_mq_unlink)
__SYSCALL(__NR_mq_timedsend)
__SYSCALL(__NR_mq_timedreceive)
__SYSCALL(__NR_mq_notify)
__SYSCALL(__NR_mq_getsetattr)
__SYSCALL(__NR_kexec_load)
__SYSCALL(__NR_waitid)
__SYSCALL(__NR_add_key)
__SYSCALL(__NR_request_key)
__SYSCALL(__NR_keyctl)
__SYSCALL(__NR_ioprio_set)
__SYSCALL(__NR_ioprio_get)
__SYSCALL(__NR_inotify_init)
__SYSCALL(__NR_inotify_add_watch)
__SYSCALL(__NR_inotify_rm_watch)
__SYSCALL(__NR_migrate_pages)
__SYSCALL(__NR_openat)
__SYSCALL(__NR_mkdirat)
__SYSCALL(__NR_mknodat)
__SYSCALL(__NR_fchownat)
__SYSCALL(__NR_futimesat)
__SYSCALL(__NR_newfstatat)
__SYSCALL(__NR_unlinkat)
__SYSCALL(__NR_renameat)
__SYSCALL(__NR_linkat)
__SYSCALL(__NR_symlinkat)
__SYSCALL(__NR_readlinkat)
__SYSCALL(__NR_fchmodat)
__SYSCALL(__NR_faccessat)
__SYSCALL(__NR_pselect6)
__SYSCALL(__NR_ppoll)
__SYSCALL(__NR_unshare)
__SYSCALL(__NR_set_robust_list)
__SYSCALL(__NR_get_robust_list)
__SYSCALL(__NR_splice)
__SYSCALL(__NR_tee)
__SYSCALL(__NR_sync_file_range)
__SYSCALL(__NR_vmsplice)
__SYSCALL(__NR_move_pages)
__SYSCALL(__NR_utimensat)
__SYSCALL(__NR_epoll_pwait)
__SYSCALL(__NR_signalfd)
__SYSCALL(__NR_timerfd_create)
__SYSCALL(__NR_eventfd)
__SYSCALL(__NR_fallocate)
__SYSCALL(__NR_timerfd_settime)
__SYSCALL(__NR_timerfd_gettime)
__SYSCALL(__NR_accept4)
__SYSCALL(__NR_signalfd4)
__SYSCALL(__NR_eventfd2)
__SYSCALL(__NR_epoll_create1)
__SYSCALL(__NR_dup3)
__SYSCALL(__NR_pipe2)
__SYSCALL(__NR_inotify_init1)
__SYSCALL(__NR_preadv)
__SYSCALL(__NR_pwritev)
__SYSCALL(__NR_rt_tgsigqueueinfo)
__SYSCALL(__NR_perf_event_open)
__SYSCALL(__NR_recvmmsg)
__SYSCALL(__NR_fanotify_init)
__SYSCALL(__NR_fanotify_mark)
__SYSCALL(__NR_prlimit64)
__SYSCALL(__NR_name_to_handle_at)
__SYSCALL(__NR_open_by_handle_at)
__SYSCALL(__NR_clock_adjtime)
__SYSCALL(__NR_syncfs)
__SYSCALL(__NR_sendmmsg)
__SYSCALL(__NR_setns)
__SYSCALL(__NR_getcpu)
__SYSCALL(__NR_process_vm_readv)
__SYSCALL(__NR_process_vm_writev)


#endif /* _ASM_X86_UNISTD_64_H */
