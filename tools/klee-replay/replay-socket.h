#ifndef __REPLAY_SOCKET_H__
#define __REPLAY_SOCKET_H__

#include <sys/types.h>

void open_socket_files();
int assign_datagram(pid_t child);
void process_socketcall(pid_t child, int before);

#endif
