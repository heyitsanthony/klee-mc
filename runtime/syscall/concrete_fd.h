#ifndef KLEEMC_FD_H
#define KLEEMC_FD_H

#include <sys/types.h>
#include <sys/stat.h>

void fd_init(void);
int fd_open(const char*);
int fd_open_sym(void);
void fd_close(int);
int fd_is_concrete(int);
ssize_t fd_read(int fd, char*, int c);
ssize_t fd_pread(int fd, void* buf, size_t c, off_t off);
int fd_stat(int fd, struct stat* s);
off_t fd_lseek(int, off_t, int whence);

#endif
