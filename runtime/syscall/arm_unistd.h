/*
 *  arch/arm/include/asm/unistd.h
 *
 *  Copyright (C) 2001-2005 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Please forward _all_ changes to this file to rmk@arm.linux.org.uk,
 * no matter what the change is.  Thanks!
 */
#ifndef __ASM_ARM_UNISTD_H
#define __ASM_ARM_UNISTD_H

#define ARM__NR_OABI_SYSCALL_BASE	0x900000
#define __ARM_EABI__	1

#if defined(__thumb__) || defined(__ARM_EABI__)
#define ARM__NR_SYSCALL_BASE	0
#else
#define ARM__NR_SYSCALL_BASE	ARM__NR_OABI_SYSCALL_BASE
#endif

/*
 * This file contains the system call numbers.
 */

#define ARM__NR_restart_syscall		(ARM__NR_SYSCALL_BASE+  0)
#define ARM__NR_exit			(ARM__NR_SYSCALL_BASE+  1)
#define ARM__NR_fork			(ARM__NR_SYSCALL_BASE+  2)
#define ARM__NR_read			(ARM__NR_SYSCALL_BASE+  3)
#define ARM__NR_write			(ARM__NR_SYSCALL_BASE+  4)
#define ARM__NR_open			(ARM__NR_SYSCALL_BASE+  5)
#define ARM__NR_close			(ARM__NR_SYSCALL_BASE+  6)
					/* 7 was sys_waitpid */
#define ARM__NR_creat			(ARM__NR_SYSCALL_BASE+  8)
#define ARM__NR_link			(ARM__NR_SYSCALL_BASE+  9)
#define ARM__NR_unlink			(ARM__NR_SYSCALL_BASE+ 10)
#define ARM__NR_execve			(ARM__NR_SYSCALL_BASE+ 11)
#define ARM__NR_chdir			(ARM__NR_SYSCALL_BASE+ 12)
#define ARM__NR_time			(ARM__NR_SYSCALL_BASE+ 13)
#define ARM__NR_mknod			(ARM__NR_SYSCALL_BASE+ 14)
#define ARM__NR_chmod			(ARM__NR_SYSCALL_BASE+ 15)
#define ARM__NR_lchown			(ARM__NR_SYSCALL_BASE+ 16)
					/* 17 was sys_break */
					/* 18 was sys_stat */
#define ARM__NR_lseek			(ARM__NR_SYSCALL_BASE+ 19)
#define ARM__NR_getpid			(ARM__NR_SYSCALL_BASE+ 20)
#define ARM__NR_mount			(ARM__NR_SYSCALL_BASE+ 21)
#define ARM__NR_umount			(ARM__NR_SYSCALL_BASE+ 22)
#define ARM__NR_setuid			(ARM__NR_SYSCALL_BASE+ 23)
#define ARM__NR_getuid			(ARM__NR_SYSCALL_BASE+ 24)
#define ARM__NR_stime			(ARM__NR_SYSCALL_BASE+ 25)
#define ARM__NR_ptrace			(ARM__NR_SYSCALL_BASE+ 26)
#define ARM__NR_alarm			(ARM__NR_SYSCALL_BASE+ 27)
					/* 28 was sys_fstat */
#define ARM__NR_pause			(ARM__NR_SYSCALL_BASE+ 29)
#define ARM__NR_utime			(ARM__NR_SYSCALL_BASE+ 30)
					/* 31 was sys_stty */
					/* 32 was sys_gtty */
#define ARM__NR_access			(ARM__NR_SYSCALL_BASE+ 33)
#define ARM__NR_nice			(ARM__NR_SYSCALL_BASE+ 34)
					/* 35 was sys_ftime */
#define ARM__NR_sync			(ARM__NR_SYSCALL_BASE+ 36)
#define ARM__NR_kill			(ARM__NR_SYSCALL_BASE+ 37)
#define ARM__NR_rename			(ARM__NR_SYSCALL_BASE+ 38)
#define ARM__NR_mkdir			(ARM__NR_SYSCALL_BASE+ 39)
#define ARM__NR_rmdir			(ARM__NR_SYSCALL_BASE+ 40)
#define ARM__NR_dup			(ARM__NR_SYSCALL_BASE+ 41)
#define ARM__NR_pipe			(ARM__NR_SYSCALL_BASE+ 42)
#define ARM__NR_times			(ARM__NR_SYSCALL_BASE+ 43)
					/* 44 was sys_prof */
#define ARM__NR_brk			(ARM__NR_SYSCALL_BASE+ 45)
#define ARM__NR_setgid			(ARM__NR_SYSCALL_BASE+ 46)
#define ARM__NR_getgid			(ARM__NR_SYSCALL_BASE+ 47)
					/* 48 was sys_signal */
#define ARM__NR_geteuid			(ARM__NR_SYSCALL_BASE+ 49)
#define ARM__NR_getegid			(ARM__NR_SYSCALL_BASE+ 50)
#define ARM__NR_acct			(ARM__NR_SYSCALL_BASE+ 51)
#define ARM__NR_umount2			(ARM__NR_SYSCALL_BASE+ 52)
					/* 53 was sys_lock */
#define ARM__NR_ioctl			(ARM__NR_SYSCALL_BASE+ 54)
#define ARM__NR_fcntl			(ARM__NR_SYSCALL_BASE+ 55)
					/* 56 was sys_mpx */
#define ARM__NR_setpgid			(ARM__NR_SYSCALL_BASE+ 57)
					/* 58 was sys_ulimit */
					/* 59 was sys_olduname */
#define ARM__NR_umask			(ARM__NR_SYSCALL_BASE+ 60)
#define ARM__NR_chroot			(ARM__NR_SYSCALL_BASE+ 61)
#define ARM__NR_ustat			(ARM__NR_SYSCALL_BASE+ 62)
#define ARM__NR_dup2			(ARM__NR_SYSCALL_BASE+ 63)
#define ARM__NR_getppid			(ARM__NR_SYSCALL_BASE+ 64)
#define ARM__NR_getpgrp			(ARM__NR_SYSCALL_BASE+ 65)
#define ARM__NR_setsid			(ARM__NR_SYSCALL_BASE+ 66)
#define ARM__NR_sigaction			(ARM__NR_SYSCALL_BASE+ 67)
					/* 68 was sys_sgetmask */
					/* 69 was sys_ssetmask */
#define ARM__NR_setreuid			(ARM__NR_SYSCALL_BASE+ 70)
#define ARM__NR_setregid			(ARM__NR_SYSCALL_BASE+ 71)
#define ARM__NR_sigsuspend			(ARM__NR_SYSCALL_BASE+ 72)
#define ARM__NR_sigpending			(ARM__NR_SYSCALL_BASE+ 73)
#define ARM__NR_sethostname		(ARM__NR_SYSCALL_BASE+ 74)
#define ARM__NR_setrlimit			(ARM__NR_SYSCALL_BASE+ 75)
#define ARM__NR_getrlimit			(ARM__NR_SYSCALL_BASE+ 76)	/* Back compat 2GB limited rlimit */
#define ARM__NR_getrusage			(ARM__NR_SYSCALL_BASE+ 77)
#define ARM__NR_gettimeofday		(ARM__NR_SYSCALL_BASE+ 78)
#define ARM__NR_settimeofday		(ARM__NR_SYSCALL_BASE+ 79)
#define ARM__NR_getgroups			(ARM__NR_SYSCALL_BASE+ 80)
#define ARM__NR_setgroups			(ARM__NR_SYSCALL_BASE+ 81)
#define ARM__NR_select			(ARM__NR_SYSCALL_BASE+ 82)
#define ARM__NR_symlink			(ARM__NR_SYSCALL_BASE+ 83)
					/* 84 was sys_lstat */
#define ARM__NR_readlink			(ARM__NR_SYSCALL_BASE+ 85)
#define ARM__NR_uselib			(ARM__NR_SYSCALL_BASE+ 86)
#define ARM__NR_swapon			(ARM__NR_SYSCALL_BASE+ 87)
#define ARM__NR_reboot			(ARM__NR_SYSCALL_BASE+ 88)
#define ARM__NR_readdir			(ARM__NR_SYSCALL_BASE+ 89)
#define ARM__NR_mmap			(ARM__NR_SYSCALL_BASE+ 90)
#define ARM__NR_munmap			(ARM__NR_SYSCALL_BASE+ 91)
#define ARM__NR_truncate			(ARM__NR_SYSCALL_BASE+ 92)
#define ARM__NR_ftruncate			(ARM__NR_SYSCALL_BASE+ 93)
#define ARM__NR_fchmod			(ARM__NR_SYSCALL_BASE+ 94)
#define ARM__NR_fchown			(ARM__NR_SYSCALL_BASE+ 95)
#define ARM__NR_getpriority		(ARM__NR_SYSCALL_BASE+ 96)
#define ARM__NR_setpriority		(ARM__NR_SYSCALL_BASE+ 97)
					/* 98 was sys_profil */
#define ARM__NR_statfs			(ARM__NR_SYSCALL_BASE+ 99)
#define ARM__NR_fstatfs			(ARM__NR_SYSCALL_BASE+100)
#define ARM__NR_ioperm			(ARM__NR_SYSCALL_BASE+101)
#define ARM__NR_socketcall		(ARM__NR_SYSCALL_BASE+102)
#define ARM__NR_syslog			(ARM__NR_SYSCALL_BASE+103)
#define ARM__NR_setitimer		(ARM__NR_SYSCALL_BASE+104)
#define ARM__NR_getitimer		(ARM__NR_SYSCALL_BASE+105)
#define ARM__NR_stat			(ARM__NR_SYSCALL_BASE+106)
#define ARM__NR_lstat			(ARM__NR_SYSCALL_BASE+107)
#define ARM__NR_fstat			(ARM__NR_SYSCALL_BASE+108)
					/* 109 was sys_uname */
/* 110 was sys_iopl */
#define ARM__NR_iopl			(ARM__NR_SYSCALL_BASE+110)
#define ARM__NR_vhangup			(ARM__NR_SYSCALL_BASE+111)
					/* 112 was sys_idle */
#define ARM__NR_syscall			(ARM__NR_SYSCALL_BASE+113) /* syscall to call a syscall! */
#define ARM__NR_wait4			(ARM__NR_SYSCALL_BASE+114)
#define ARM__NR_swapoff			(ARM__NR_SYSCALL_BASE+115)
#define ARM__NR_sysinfo			(ARM__NR_SYSCALL_BASE+116)
#define ARM__NR_ipc			(ARM__NR_SYSCALL_BASE+117)
#define ARM__NR_fsync			(ARM__NR_SYSCALL_BASE+118)
#define ARM__NR_sigreturn			(ARM__NR_SYSCALL_BASE+119)
#define ARM__NR_clone			(ARM__NR_SYSCALL_BASE+120)
#define ARM__NR_setdomainname		(ARM__NR_SYSCALL_BASE+121)
#define ARM__NR_uname			(ARM__NR_SYSCALL_BASE+122)
					/* 123 was sys_modify_ldt */
#define ARM__NR_adjtimex			(ARM__NR_SYSCALL_BASE+124)
#define ARM__NR_mprotect			(ARM__NR_SYSCALL_BASE+125)
#define ARM__NR_sigprocmask		(ARM__NR_SYSCALL_BASE+126)
#define ARM__NR_create_module		(ARM__NR_SYSCALL_BASE+127) 
#define ARM__NR_init_module		(ARM__NR_SYSCALL_BASE+128)
#define ARM__NR_delete_module		(ARM__NR_SYSCALL_BASE+129)
#define ARM__NR_get_kernel_syms		(ARM__NR_SYSCALL_BASE+130)
					/* 130 was sys_get_kernel_syms */
#define ARM__NR_quotactl			(ARM__NR_SYSCALL_BASE+131)
#define ARM__NR_getpgid			(ARM__NR_SYSCALL_BASE+132)
#define ARM__NR_fchdir			(ARM__NR_SYSCALL_BASE+133)
#define ARM__NR_bdflush			(ARM__NR_SYSCALL_BASE+134)
#define ARM__NR_sysfs			(ARM__NR_SYSCALL_BASE+135)
#define ARM__NR_personality		(ARM__NR_SYSCALL_BASE+136)
					/* 137 was sys_afs_syscall */
#define ARM__NR_setfsuid			(ARM__NR_SYSCALL_BASE+138)
#define ARM__NR_setfsgid			(ARM__NR_SYSCALL_BASE+139)
#define ARM__NR__llseek			(ARM__NR_SYSCALL_BASE+140)
#define ARM__NR_getdents			(ARM__NR_SYSCALL_BASE+141)
#define ARM__NR__newselect			(ARM__NR_SYSCALL_BASE+142)
#define ARM__NR_flock			(ARM__NR_SYSCALL_BASE+143)
#define ARM__NR_msync			(ARM__NR_SYSCALL_BASE+144)
#define ARM__NR_readv			(ARM__NR_SYSCALL_BASE+145)
#define ARM__NR_writev			(ARM__NR_SYSCALL_BASE+146)
#define ARM__NR_getsid			(ARM__NR_SYSCALL_BASE+147)
#define ARM__NR_fdatasync			(ARM__NR_SYSCALL_BASE+148)
#define ARM__NR__sysctl			(ARM__NR_SYSCALL_BASE+149)
#define ARM__NR_mlock			(ARM__NR_SYSCALL_BASE+150)
#define ARM__NR_munlock			(ARM__NR_SYSCALL_BASE+151)
#define ARM__NR_mlockall			(ARM__NR_SYSCALL_BASE+152)
#define ARM__NR_munlockall			(ARM__NR_SYSCALL_BASE+153)
#define ARM__NR_sched_setparam		(ARM__NR_SYSCALL_BASE+154)
#define ARM__NR_sched_getparam		(ARM__NR_SYSCALL_BASE+155)
#define ARM__NR_sched_setscheduler		(ARM__NR_SYSCALL_BASE+156)
#define ARM__NR_sched_getscheduler		(ARM__NR_SYSCALL_BASE+157)
#define ARM__NR_sched_yield		(ARM__NR_SYSCALL_BASE+158)
#define ARM__NR_sched_get_priority_max	(ARM__NR_SYSCALL_BASE+159)
#define ARM__NR_sched_get_priority_min	(ARM__NR_SYSCALL_BASE+160)
#define ARM__NR_sched_rr_get_interval	(ARM__NR_SYSCALL_BASE+161)
#define ARM__NR_nanosleep			(ARM__NR_SYSCALL_BASE+162)
#define ARM__NR_mremap			(ARM__NR_SYSCALL_BASE+163)
#define ARM__NR_setresuid			(ARM__NR_SYSCALL_BASE+164)
#define ARM__NR_getresuid			(ARM__NR_SYSCALL_BASE+165)
					/* 166 was sys_vm86 */
#define ARM__NR_query_module		(ARM__NR_SYSCALL_BASE+167)
#define ARM__NR_poll			(ARM__NR_SYSCALL_BASE+168)
#define ARM__NR_nfsservctl			(ARM__NR_SYSCALL_BASE+169)
#define ARM__NR_setresgid			(ARM__NR_SYSCALL_BASE+170)
#define ARM__NR_getresgid			(ARM__NR_SYSCALL_BASE+171)
#define ARM__NR_prctl			(ARM__NR_SYSCALL_BASE+172)
#define ARM__NR_rt_sigreturn		(ARM__NR_SYSCALL_BASE+173)
#define ARM__NR_rt_sigaction		(ARM__NR_SYSCALL_BASE+174)
#define ARM__NR_rt_sigprocmask		(ARM__NR_SYSCALL_BASE+175)
#define ARM__NR_rt_sigpending		(ARM__NR_SYSCALL_BASE+176)
#define ARM__NR_rt_sigtimedwait		(ARM__NR_SYSCALL_BASE+177)
#define ARM__NR_rt_sigqueueinfo		(ARM__NR_SYSCALL_BASE+178)
#define ARM__NR_rt_sigsuspend		(ARM__NR_SYSCALL_BASE+179)
#define ARM__NR_pread64			(ARM__NR_SYSCALL_BASE+180)
#define ARM__NR_pwrite64			(ARM__NR_SYSCALL_BASE+181)
#define ARM__NR_chown			(ARM__NR_SYSCALL_BASE+182)
#define ARM__NR_getcwd			(ARM__NR_SYSCALL_BASE+183)
#define ARM__NR_capget			(ARM__NR_SYSCALL_BASE+184)
#define ARM__NR_capset			(ARM__NR_SYSCALL_BASE+185)
#define ARM__NR_sigaltstack		(ARM__NR_SYSCALL_BASE+186)
#define ARM__NR_sendfile			(ARM__NR_SYSCALL_BASE+187)
					/* 188 reserved */
					/* 189 reserved */
#define ARM__NR_vfork			(ARM__NR_SYSCALL_BASE+190)
#define ARM__NR_ugetrlimit			(ARM__NR_SYSCALL_BASE+191)	/* SuS compliant getrlimit */
#define ARM__NR_mmap2			(ARM__NR_SYSCALL_BASE+192)
#define ARM__NR_truncate64			(ARM__NR_SYSCALL_BASE+193)
#define ARM__NR_ftruncate64		(ARM__NR_SYSCALL_BASE+194)
#define ARM__NR_stat64			(ARM__NR_SYSCALL_BASE+195)
#define ARM__NR_lstat64			(ARM__NR_SYSCALL_BASE+196)
#define ARM__NR_fstat64			(ARM__NR_SYSCALL_BASE+197)
#define ARM__NR_lchown32			(ARM__NR_SYSCALL_BASE+198)
#define ARM__NR_getuid32			(ARM__NR_SYSCALL_BASE+199)
#define ARM__NR_getgid32			(ARM__NR_SYSCALL_BASE+200)
#define ARM__NR_geteuid32			(ARM__NR_SYSCALL_BASE+201)
#define ARM__NR_getegid32			(ARM__NR_SYSCALL_BASE+202)
#define ARM__NR_setreuid32			(ARM__NR_SYSCALL_BASE+203)
#define ARM__NR_setregid32			(ARM__NR_SYSCALL_BASE+204)
#define ARM__NR_getgroups32		(ARM__NR_SYSCALL_BASE+205)
#define ARM__NR_setgroups32		(ARM__NR_SYSCALL_BASE+206)
#define ARM__NR_fchown32			(ARM__NR_SYSCALL_BASE+207)
#define ARM__NR_setresuid32		(ARM__NR_SYSCALL_BASE+208)
#define ARM__NR_getresuid32		(ARM__NR_SYSCALL_BASE+209)
#define ARM__NR_setresgid32		(ARM__NR_SYSCALL_BASE+210)
#define ARM__NR_getresgid32		(ARM__NR_SYSCALL_BASE+211)
#define ARM__NR_chown32			(ARM__NR_SYSCALL_BASE+212)
#define ARM__NR_setuid32			(ARM__NR_SYSCALL_BASE+213)
#define ARM__NR_setgid32			(ARM__NR_SYSCALL_BASE+214)
#define ARM__NR_setfsuid32			(ARM__NR_SYSCALL_BASE+215)
#define ARM__NR_setfsgid32			(ARM__NR_SYSCALL_BASE+216)
#define ARM__NR_getdents64			(ARM__NR_SYSCALL_BASE+217)
#define ARM__NR_pivot_root			(ARM__NR_SYSCALL_BASE+218)
#define ARM__NR_mincore			(ARM__NR_SYSCALL_BASE+219)
#define ARM__NR_madvise			(ARM__NR_SYSCALL_BASE+220)
#define ARM__NR_fcntl64			(ARM__NR_SYSCALL_BASE+221)
#define ARM__NR_tuxcall			(ARM__NR_SYSCALL_BASE+222)
					/* 223 is unused */
#define ARM__NR_gettid			(ARM__NR_SYSCALL_BASE+224)
#define ARM__NR_readahead			(ARM__NR_SYSCALL_BASE+225)
#define ARM__NR_setxattr			(ARM__NR_SYSCALL_BASE+226)
#define ARM__NR_lsetxattr			(ARM__NR_SYSCALL_BASE+227)
#define ARM__NR_fsetxattr			(ARM__NR_SYSCALL_BASE+228)
#define ARM__NR_getxattr			(ARM__NR_SYSCALL_BASE+229)
#define ARM__NR_lgetxattr			(ARM__NR_SYSCALL_BASE+230)
#define ARM__NR_fgetxattr			(ARM__NR_SYSCALL_BASE+231)
#define ARM__NR_listxattr			(ARM__NR_SYSCALL_BASE+232)
#define ARM__NR_llistxattr			(ARM__NR_SYSCALL_BASE+233)
#define ARM__NR_flistxattr			(ARM__NR_SYSCALL_BASE+234)
#define ARM__NR_removexattr		(ARM__NR_SYSCALL_BASE+235)
#define ARM__NR_lremovexattr		(ARM__NR_SYSCALL_BASE+236)
#define ARM__NR_fremovexattr		(ARM__NR_SYSCALL_BASE+237)
#define ARM__NR_tkill			(ARM__NR_SYSCALL_BASE+238)
#define ARM__NR_sendfile64			(ARM__NR_SYSCALL_BASE+239)
#define ARM__NR_futex			(ARM__NR_SYSCALL_BASE+240)
#define ARM__NR_sched_setaffinity		(ARM__NR_SYSCALL_BASE+241)
#define ARM__NR_sched_getaffinity		(ARM__NR_SYSCALL_BASE+242)
#define ARM__NR_io_setup			(ARM__NR_SYSCALL_BASE+243)
#define ARM__NR_io_destroy			(ARM__NR_SYSCALL_BASE+244)
#define ARM__NR_io_getevents		(ARM__NR_SYSCALL_BASE+245)
#define ARM__NR_io_submit			(ARM__NR_SYSCALL_BASE+246)
#define ARM__NR_io_cancel			(ARM__NR_SYSCALL_BASE+247)
#define ARM__NR_exit_group			(ARM__NR_SYSCALL_BASE+248)
#define ARM__NR_lookup_dcookie		(ARM__NR_SYSCALL_BASE+249)
#define ARM__NR_epoll_create		(ARM__NR_SYSCALL_BASE+250)
#define ARM__NR_epoll_ctl			(ARM__NR_SYSCALL_BASE+251)
#define ARM__NR_epoll_wait			(ARM__NR_SYSCALL_BASE+252)
#define ARM__NR_remap_file_pages		(ARM__NR_SYSCALL_BASE+253)
#define ARM__NR_set_thread_area		(ARM__NR_SYSCALL_BASE+254)
#define ARM__NR_get_thread_area		(ARM__NR_SYSCALL_BASE+255)
#define ARM__NR_set_tid_address		(ARM__NR_SYSCALL_BASE+256)
#define ARM__NR_timer_create		(ARM__NR_SYSCALL_BASE+257)
#define ARM__NR_timer_settime		(ARM__NR_SYSCALL_BASE+258)
#define ARM__NR_timer_gettime		(ARM__NR_SYSCALL_BASE+259)
#define ARM__NR_timer_getoverrun		(ARM__NR_SYSCALL_BASE+260)
#define ARM__NR_timer_delete		(ARM__NR_SYSCALL_BASE+261)
#define ARM__NR_clock_settime		(ARM__NR_SYSCALL_BASE+262)
#define ARM__NR_clock_gettime		(ARM__NR_SYSCALL_BASE+263)
#define ARM__NR_clock_getres		(ARM__NR_SYSCALL_BASE+264)
#define ARM__NR_clock_nanosleep		(ARM__NR_SYSCALL_BASE+265)
#define ARM__NR_statfs64			(ARM__NR_SYSCALL_BASE+266)
#define ARM__NR_fstatfs64			(ARM__NR_SYSCALL_BASE+267)
#define ARM__NR_tgkill			(ARM__NR_SYSCALL_BASE+268)
#define ARM__NR_utimes			(ARM__NR_SYSCALL_BASE+269)
#define ARM__NR_fadvise64		(ARM__NR_SYSCALL_BASE+270)
#define ARM__NR_pciconfig_iobase		(ARM__NR_SYSCALL_BASE+271)
#define ARM__NR_pciconfig_read		(ARM__NR_SYSCALL_BASE+272)
#define ARM__NR_pciconfig_write		(ARM__NR_SYSCALL_BASE+273)
#define ARM__NR_mq_open			(ARM__NR_SYSCALL_BASE+274)
#define ARM__NR_mq_unlink			(ARM__NR_SYSCALL_BASE+275)
#define ARM__NR_mq_timedsend		(ARM__NR_SYSCALL_BASE+276)
#define ARM__NR_mq_timedreceive		(ARM__NR_SYSCALL_BASE+277)
#define ARM__NR_mq_notify			(ARM__NR_SYSCALL_BASE+278)
#define ARM__NR_mq_getsetattr		(ARM__NR_SYSCALL_BASE+279)
#define ARM__NR_waitid			(ARM__NR_SYSCALL_BASE+280)
#define ARM__NR_socket			(ARM__NR_SYSCALL_BASE+281)
#define ARM__NR_bind			(ARM__NR_SYSCALL_BASE+282)
#define ARM__NR_connect			(ARM__NR_SYSCALL_BASE+283)
#define ARM__NR_listen			(ARM__NR_SYSCALL_BASE+284)
#define ARM__NR_accept			(ARM__NR_SYSCALL_BASE+285)
#define ARM__NR_getsockname		(ARM__NR_SYSCALL_BASE+286)
#define ARM__NR_getpeername		(ARM__NR_SYSCALL_BASE+287)
#define ARM__NR_socketpair			(ARM__NR_SYSCALL_BASE+288)
#define ARM__NR_send			(ARM__NR_SYSCALL_BASE+289)
#define ARM__NR_sendto			(ARM__NR_SYSCALL_BASE+290)
#define ARM__NR_recv			(ARM__NR_SYSCALL_BASE+291)
#define ARM__NR_recvfrom			(ARM__NR_SYSCALL_BASE+292)
#define ARM__NR_shutdown			(ARM__NR_SYSCALL_BASE+293)
#define ARM__NR_setsockopt			(ARM__NR_SYSCALL_BASE+294)
#define ARM__NR_getsockopt			(ARM__NR_SYSCALL_BASE+295)
#define ARM__NR_sendmsg			(ARM__NR_SYSCALL_BASE+296)
#define ARM__NR_recvmsg			(ARM__NR_SYSCALL_BASE+297)
#define ARM__NR_semop			(ARM__NR_SYSCALL_BASE+298)
#define ARM__NR_semget			(ARM__NR_SYSCALL_BASE+299)
#define ARM__NR_semctl			(ARM__NR_SYSCALL_BASE+300)
#define ARM__NR_msgsnd			(ARM__NR_SYSCALL_BASE+301)
#define ARM__NR_msgrcv			(ARM__NR_SYSCALL_BASE+302)
#define ARM__NR_msgget			(ARM__NR_SYSCALL_BASE+303)
#define ARM__NR_msgctl			(ARM__NR_SYSCALL_BASE+304)
#define ARM__NR_shmat			(ARM__NR_SYSCALL_BASE+305)
#define ARM__NR_shmdt			(ARM__NR_SYSCALL_BASE+306)
#define ARM__NR_shmget			(ARM__NR_SYSCALL_BASE+307)
#define ARM__NR_shmctl			(ARM__NR_SYSCALL_BASE+308)
#define ARM__NR_add_key			(ARM__NR_SYSCALL_BASE+309)
#define ARM__NR_request_key		(ARM__NR_SYSCALL_BASE+310)
#define ARM__NR_keyctl			(ARM__NR_SYSCALL_BASE+311)
#define ARM__NR_semtimedop			(ARM__NR_SYSCALL_BASE+312)
#define ARM__NR_vserver			(ARM__NR_SYSCALL_BASE+313)
#define ARM__NR_ioprio_set			(ARM__NR_SYSCALL_BASE+314)
#define ARM__NR_ioprio_get			(ARM__NR_SYSCALL_BASE+315)
#define ARM__NR_inotify_init		(ARM__NR_SYSCALL_BASE+316)
#define ARM__NR_inotify_add_watch		(ARM__NR_SYSCALL_BASE+317)
#define ARM__NR_inotify_rm_watch		(ARM__NR_SYSCALL_BASE+318)
#define ARM__NR_mbind			(ARM__NR_SYSCALL_BASE+319)
#define ARM__NR_get_mempolicy		(ARM__NR_SYSCALL_BASE+320)
#define ARM__NR_set_mempolicy		(ARM__NR_SYSCALL_BASE+321)
#define ARM__NR_openat			(ARM__NR_SYSCALL_BASE+322)
#define ARM__NR_mkdirat			(ARM__NR_SYSCALL_BASE+323)
#define ARM__NR_mknodat			(ARM__NR_SYSCALL_BASE+324)
#define ARM__NR_fchownat			(ARM__NR_SYSCALL_BASE+325)
#define ARM__NR_futimesat			(ARM__NR_SYSCALL_BASE+326)
#define ARM__NR_fstatat64			(ARM__NR_SYSCALL_BASE+327)
#define ARM__NR_unlinkat			(ARM__NR_SYSCALL_BASE+328)
#define ARM__NR_renameat			(ARM__NR_SYSCALL_BASE+329)
#define ARM__NR_linkat			(ARM__NR_SYSCALL_BASE+330)
#define ARM__NR_symlinkat			(ARM__NR_SYSCALL_BASE+331)
#define ARM__NR_readlinkat			(ARM__NR_SYSCALL_BASE+332)
#define ARM__NR_fchmodat			(ARM__NR_SYSCALL_BASE+333)
#define ARM__NR_faccessat			(ARM__NR_SYSCALL_BASE+334)
#define ARM__NR_pselect6			(ARM__NR_SYSCALL_BASE+335)
#define ARM__NR_ppoll			(ARM__NR_SYSCALL_BASE+336)
#define ARM__NR_unshare			(ARM__NR_SYSCALL_BASE+337)
#define ARM__NR_set_robust_list		(ARM__NR_SYSCALL_BASE+338)
#define ARM__NR_get_robust_list		(ARM__NR_SYSCALL_BASE+339)
#define ARM__NR_splice			(ARM__NR_SYSCALL_BASE+340)
//#define ARM__NR_arm_sync_file_range	(ARM__NR_SYSCALL_BASE+341)
//#define ARM__NR_sync_file_range2	ARM__NR_arm_sync_file_range
#define ARM__NR_sync_file_range		(ARM__NR_SYSCALL_BASE+341)
#define ARM__NR_tee			(ARM__NR_SYSCALL_BASE+342)
#define ARM__NR_vmsplice			(ARM__NR_SYSCALL_BASE+343)
#define ARM__NR_move_pages			(ARM__NR_SYSCALL_BASE+344)
#define ARM__NR_getcpu			(ARM__NR_SYSCALL_BASE+345)
#define ARM__NR_epoll_pwait		(ARM__NR_SYSCALL_BASE+346)
#define ARM__NR_kexec_load			(ARM__NR_SYSCALL_BASE+347)
#define ARM__NR_utimensat			(ARM__NR_SYSCALL_BASE+348)
#define ARM__NR_signalfd			(ARM__NR_SYSCALL_BASE+349)
#define ARM__NR_timerfd_create		(ARM__NR_SYSCALL_BASE+350)
#define ARM__NR_eventfd			(ARM__NR_SYSCALL_BASE+351)
#define ARM__NR_fallocate			(ARM__NR_SYSCALL_BASE+352)
#define ARM__NR_timerfd_settime		(ARM__NR_SYSCALL_BASE+353)
#define ARM__NR_timerfd_gettime		(ARM__NR_SYSCALL_BASE+354)
#define ARM__NR_signalfd4			(ARM__NR_SYSCALL_BASE+355)
#define ARM__NR_eventfd2			(ARM__NR_SYSCALL_BASE+356)
#define ARM__NR_epoll_create1		(ARM__NR_SYSCALL_BASE+357)
#define ARM__NR_dup3			(ARM__NR_SYSCALL_BASE+358)
#define ARM__NR_pipe2			(ARM__NR_SYSCALL_BASE+359)
#define ARM__NR_inotify_init1		(ARM__NR_SYSCALL_BASE+360)
#define ARM__NR_preadv			(ARM__NR_SYSCALL_BASE+361)
#define ARM__NR_pwritev			(ARM__NR_SYSCALL_BASE+362)
#define ARM__NR_rt_tgsigqueueinfo		(ARM__NR_SYSCALL_BASE+363)
#define ARM__NR_perf_event_open		(ARM__NR_SYSCALL_BASE+364)
#define ARM__NR_recvmmsg			(ARM__NR_SYSCALL_BASE+365)
#define ARM__NR_accept4			(ARM__NR_SYSCALL_BASE+366)
#define ARM__NR_fanotify_init		(ARM__NR_SYSCALL_BASE+367)
#define ARM__NR_fanotify_mark		(ARM__NR_SYSCALL_BASE+368)
#define ARM__NR_prlimit64			(ARM__NR_SYSCALL_BASE+369)
#define ARM__NR_name_to_handle_at		(ARM__NR_SYSCALL_BASE+370)
#define ARM__NR_open_by_handle_at		(ARM__NR_SYSCALL_BASE+371)
#define ARM__NR_clock_adjtime		(ARM__NR_SYSCALL_BASE+372)
#define ARM__NR_syncfs			(ARM__NR_SYSCALL_BASE+373)
#define ARM__NR_sendmmsg			(ARM__NR_SYSCALL_BASE+374)
#define ARM__NR_setns			(ARM__NR_SYSCALL_BASE+375)

/*
 * The following SWIs are ARM private.
 */
#define __ARM_NR_BASE			(ARM__NR_SYSCALL_BASE+0x0f0000)
#define __ARM_NR_breakpoint		(__ARM_NR_BASE+1)
#define __ARM_NR_cacheflush		(__ARM_NR_BASE+2)
#define __ARM_NR_usr26			(__ARM_NR_BASE+3)
#define __ARM_NR_usr32			(__ARM_NR_BASE+4)
#define __ARM_NR_set_tls		(__ARM_NR_BASE+5)

/*
 * *NOTE*: This is a ghost syscall private to the kernel.  Only the
 * __kuser_cmpxchg code in entry-armv.S should be aware of its
 * existence.  Don't ever use this from user code.
 */

/*
 * The following syscalls are obsolete and no longer available for EABI.
 */
#if defined(__ARM_EABI__) /* && !defined(__KERNEL__) */
//#undef ARM__NR_time
#undef ARM__NR_umount
#undef ARM__NR_stime
//#undef ARM__NR_alarm
//#undef ARM__NR_utime
#undef ARM__NR_getrlimit
#undef ARM__NR_select
#undef ARM__NR_readdir
#undef ARM__NR_mmap
#undef ARM__NR_socketcall
#undef ARM__NR_syscall
#undef ARM__NR_ipc
#define ARM__NR_getrlimit	ARM__NR_ugetrlimit
#define ARM__NR_select		ARM__NR__newselect
#define ARM__NR_newfstatat	ARM__NR_fstatat64
#endif

#endif /* __ASM_ARM_UNISTD_H */
