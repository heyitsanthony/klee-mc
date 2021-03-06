#ifndef STRUCTSZ_H
#define STRUCTSZ_H

#include <sys/epoll.h>


// play with sigset and you'll see it doesn't touch all of sigset_t
// only the first 8 bytes (won't record any more than that!). Ugh!
#define SIGSET_T_SZ		16 // sizeof(sigset_t)

/* it'd be cool if I could auto-generate all of this */
#ifdef GUEST_ARCH_AMD64
#define TIMESPEC_SZ	sizeof(struct timespec)
#define RUSAGE_SZ	sizeof(struct rusage)
#define RLIMIT_SZ	sizeof(struct rlimit)
#define TIMEVAL_SZ	sizeof(struct timeval)
#define SYSINFO_SZ	sizeof(struct sysinfo)
#define TMS_SZ		sizeof(struct tms)
#define USTAT_SZ	sizeof(struct ustat)
#define ITIMERVAL_SZ	sizeof(struct itimerval)
#define STATFS_SZ	sizeof(struct statfs)
#define PTRACE_REGS_SZ		sizeof (struct user_regs_struct)
#define PTRACE_FPREGS_SZ	sizeof (struct user_fpregs_struct)
#define SIGINFO_T_SZ		sizeof (siginfo_t)
#define STAT_SZ			sizeof(struct stat)
#define EPOLL_EVENT_SZ		sizeof(struct epoll_event)
#else
#define TIMESPEC_SZ		8
#define RUSAGE_SZ		72
#define RLIMIT_SZ		8
#define TIMEVAL_SZ		8
#define SYSINFO_SZ		64
#define TMS_SZ			16
#define USTAT_SZ		20
#define ITIMERVAL_SZ		16
#define STATFS_SZ		64
/* XXX: LOL NOT FOR ARM!! */
#define PTRACE_REGS_SZ		68
#define PTRACE_FPREGS_SZ	108
#define SIGINFO_T_SZ		128
#define STAT_SZ			88
#define EPOLL_EVENT_SZ		16
#endif

#define __STAT_SZ_AMD64		96

#endif
