#ifndef __REPLAY_UTIL_H__
#define __REPLAY_UTIL_H__

#include <stddef.h>
#include <sys/types.h>

void get_data(pid_t child, const long* addr, void* buf, size_t len);
void put_data(pid_t child, long* addr, const void* buf, size_t len);
void set_data(pid_t child, long* addr, char byte, size_t len);
char* get_string(pid_t child, unsigned* addr);
void read_into_buf(pid_t child, int fd, long* addr, size_t len);

#endif
