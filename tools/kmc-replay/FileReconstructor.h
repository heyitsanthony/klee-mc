#ifndef FILERECONS_H
#define FILERECONS_H

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <map>

class FileReconstructor
{
public:
	FileReconstructor();
	virtual ~FileReconstructor();

	void pread(int vfd, void* buf, size_t count, off_t off);
	void read(int vfd, void* buf, size_t count);
	void seek(int vfd, off_t offset, int whence);
	void close(int vfd);
private:
	int getFD(int vfd);
	int getGen(int vfd);
	void limitSize(int vfd, uint64_t off);

	struct vfd_t {
		int	fd;
		int	vfd;
		int	gen;
	};

	typedef std::map<int, struct vfd_t>	vfd2fd_ty;
	vfd2fd_ty			vfd2fd;	/* replay fd -> open fd */
	unsigned int			fd_generation;
	uint64_t			max_size;
	bool				ign_close;
	std::string			file_prefix;
};

#endif
