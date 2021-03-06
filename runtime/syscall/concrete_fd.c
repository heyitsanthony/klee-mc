/**
 * Because the POSIX model is Cristi code
 */
#include <klee/klee.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include "concrete_fd.h"

#define FD_FL_IN_USE	1
#define FD_FL_CONCRETE	2

extern bool concrete_vfs;

long kmc_io(int sys_nr, long p1, long p2, long p3, long p4);
#define KMC_IO_OPEN(x)		kmc_io(SYS_open, (long)x, 0, 0, 0)
#define KMC_IO_CLOSE(x)		kmc_io(SYS_close, x, 0, 0, 0)
#define KMC_IO_PREAD(x,y,z,w)	kmc_io(SYS_pread64, x, (long)y, z, w)
#define KMC_IO_FSTAT(x,y)	kmc_io(SYS_fstat, x, (long)y, 0, 0)
#define KMC_IO_MMAP(x,y,z,w)	kmc_io(SYS_mmap,(long)x,(long)y,(long)z,(long)w)

/*
 * We try to pass everything through when possible.
 */
struct fd_info
{
	int		fi_vfd;
	int		fi_flags;
	uint64_t	fi_size;
	uint64_t	fi_cursor;
};

static uint64_t concretize_u64(uint64_t x)
{
	uint64_t	y = klee_get_value(x);
	klee_assume_eq(x, y);
	return y;
}

#define fd2fi(x)		(&fd_tab[x])
#define fi_is_concrete(x)	((fd2fi(x)->fi_flags & FD_FL_CONCRETE) != 0)
#define fi_is_used(x)		((fd2fi(x)->fi_flags & FD_FL_IN_USE) != 0)
#define fd_valid(x)		(x >= 0 && x < MAX_FD)

#define MAX_FD	1024
static struct fd_info fd_tab[MAX_FD] =
{
	[0] = {.fi_flags = FD_FL_IN_USE},
	/* mark stdout and stderr as concrete to avoid forking on ioctl, etc */
	[1] = {.fi_vfd = -1, .fi_flags = FD_FL_IN_USE | FD_FL_CONCRETE},
	[2] = {.fi_vfd = -1, .fi_flags = FD_FL_IN_USE | FD_FL_CONCRETE}
};
static int next_free_fd = 3;

static int find_next_free(void)
{
	int	k;
	for (k = next_free_fd; k < MAX_FD; k++) {
		if (!fi_is_used(k)) {
			next_free_fd = k;
			return k;
		}
	}

	if (k == MAX_FD) {
		klee_uerror("Ran out of virtual fds", "fd.err");
	}

	return -1;
}

int fd_open(const char* path)
{
	struct fd_info	*fi;
	struct stat	s;
	int		new_fd, host_fd;

	host_fd = KMC_IO_OPEN(path);
	if (host_fd < 0)
		return -1;

	new_fd = find_next_free();
	assert (!fi_is_used(new_fd));

	fi = fd2fi(new_fd);
	fi->fi_vfd = host_fd;
	fi->fi_flags = FD_FL_IN_USE | FD_FL_CONCRETE;
	fi->fi_cursor = 0;

	memset(&s, 0, sizeof(struct stat));
	fd_stat(new_fd, &s);
	fi->fi_size = s.st_size;

	return new_fd;
}

int fd_open_sym(void)
{
	struct fd_info	*fi;
	int		new_fd;

	new_fd = find_next_free();
	assert (!fi_is_used(new_fd));

	fi = fd2fi(new_fd);
	fi->fi_vfd = -1;
	fi->fi_flags = FD_FL_IN_USE;

	return new_fd;
}

void fd_close(int fd)
{
	if (!fd_valid(fd))
		return;

	if (!fi_is_used(fd))
		return;

	if (fi_is_concrete(fd))
		KMC_IO_CLOSE(fd2fi(fd)->fi_vfd);

	fd2fi(fd)->fi_flags = 0;
	if (fd < next_free_fd)
		next_free_fd = fd;
}

int fd_is_concrete(int fd)
{
	if (concrete_vfs && fd < 3) return 1;

	/* XXX: is there ever a case where the fd is
	 * symbolic but the underlying files are concrete? */
	if (klee_is_symbolic(fd))
		return 0;

	if (!fd_valid(fd))
		return 0;

	return fi_is_concrete(fd);
}

int fd_dup(int fd, int fd_min)
{
	int	k;
		
	for (k = fd_min; k < MAX_FD; k++) {
		if (!fi_is_used(k)) {
			memcpy(fd2fi(k), fd2fi(fd), sizeof(struct fd_info));
			break;
		}
	}

	if (k == MAX_FD) return -1;
	return k;
}

int fd_dup2(int oldfd, int newfd)
{
	if (!fi_is_used(oldfd)) {
		klee_print_expr("[fd] tried to dup2 on closed fd", oldfd);
		klee_ureport("dup2 on closed fd", "dup2.warn");
		return -1;
	}

	if (oldfd == newfd) {
		return oldfd;
	}

	fd_close(newfd);
	return fd_dup(oldfd, newfd);
}

ssize_t fd_read(int fd, char* buf, int c)
{
	struct fd_info	*fi;
	ssize_t		br;

	if (!fd_valid(fd))
		return -1;

	fi = fd2fi(fd);
	c = concretize_u64(c);
	buf = (void*)concretize_u64((uint64_t)buf);
	br = KMC_IO_PREAD(fi->fi_vfd, buf, c, fi->fi_cursor);
	klee_print_expr("[kmc-io] read concrete bytes", br);

	if (br > 0)
		fi->fi_cursor += br;

	return br;
}

ssize_t fd_pread(int fd, void* buf, size_t c, off_t off)
{
	c = concretize_u64(c);
	off = concretize_u64(off);
	return KMC_IO_PREAD(fd2fi(fd)->fi_vfd, buf, c, off);
}

int fd_stat(int fd, struct stat* s)
{
	if (!fi_is_used(fd))
		return -1;

	return KMC_IO_FSTAT(fd2fi(fd)->fi_vfd, s);
}

off_t fd_lseek(int fd, off_t o, int whence)
{
	struct fd_info	*fi;

	if (!fd_valid(fd))
		return -1;

	if (!fi_is_used(fd))
		return (off_t)-1;

	fi = fd2fi(fd);

	if (whence == SEEK_SET) {
		fi->fi_cursor = o;
		if (fi->fi_cursor > fi->fi_size)
			fi->fi_cursor = fi->fi_size;
		return fi->fi_cursor;
	}

	if (whence == SEEK_CUR) {
		fi->fi_cursor += o;
		if (fi->fi_cursor > fi->fi_size)
			fi->fi_cursor = fi->fi_size;
		return fi->fi_cursor;
	}

	if (whence == SEEK_END) {
		fi->fi_cursor = fi->fi_size;
		return fi->fi_cursor;
	}

	/* some weirdo seek (SEEK_HOLE, SEEK_DATA) no one cares about */
	return (off_t)-1;
}

void fd_mark(int fd, void* addr, size_t len, off_t off)
{ KMC_IO_MMAP(addr, fd2fi(fd)->fi_vfd, len, off); }
