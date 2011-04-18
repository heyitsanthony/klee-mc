#ifndef __REPLAY_FD_H__
#define __REPLAY_FD_H__

#include <sys/types.h>

/* boolean vector, specifying what fds should be ignored */
#define MAX_TOTAL_FDS 4096
struct fd_attrs {
  unsigned external  : 1;
  unsigned socket    : 1;
  unsigned datagram  : 1;
  unsigned listening : 1;
};
extern struct fd_attrs fd_attrs[MAX_TOTAL_FDS];

void process_open(pid_t child);
void process_open_done(pid_t child);
void process_close(pid_t child);
void process_read(pid_t child, int before);
void process_write(pid_t child);
void process_dup(pid_t child, int before);
void process_chmod(pid_t child);
void process_fchmod(pid_t child);
void process_ftruncate(pid_t child);
void process_getcwd(pid_t child);
void process_kill(pid_t child);
void process_chroot(pid_t child);
void process_select(pid_t child, int before);
void process_ioctl(pid_t child, int before);

#endif
