#ifndef KLEE_MEMFILE_H
#define KLEE_MEMFILE_H

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace klee
{
class MemFile
{
public:
	virtual ~MemFile(void)
	{
		munmap(cache_buf, cache_bytes);
		close(cache_fd);
	}

	const void* getBuf(void) const { return cache_buf; }
	unsigned getNumBytes(void) const { return cache_bytes; }

	static MemFile* createMode(const char* fname, int mode)
	{
		struct stat	s;
		int		fd;
		void		*buf;

		if (stat(fname, &s) != 0) return NULL;
		if (s.st_size == 0) return NULL;

		fd = open(fname, (mode & PROT_WRITE) ? O_RDWR : O_RDONLY);
		if (fd == -1) return NULL;

		buf = mmap(NULL, s.st_size, mode, MAP_SHARED, fd, 0);
		if (buf == NULL) {
			close(fd);
			return NULL;
		}

		return new MemFile(buf, s.st_size, fd);
	}

	static MemFile* create(const char* fname)
	{ return createMode(fname, PROT_READ); }

	static MemFile* createWr(const char* fname)
	{ return createMode(fname, PROT_READ | PROT_WRITE); }


protected:
	MemFile(void* _cbuf, unsigned _cbytes, int _fd)
	: cache_buf(_cbuf)
	, cache_bytes(_cbytes)
	, cache_fd(_fd) {}

private:
	void		*cache_buf;
	unsigned 	cache_bytes;
	int		cache_fd;
};

}

#endif
