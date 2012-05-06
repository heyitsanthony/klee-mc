#ifndef FILERECONS_H
#define FILERECONS_H

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <map>

class FileReconstructor
{
public:
	FileReconstructor();
	virtual ~FileReconstructor();

	void read(int vfd, void* buf, size_t count);
	void seek(int vfd, off_t offset, int whence);
	void close(int vfd);
private:
	int getFD(int vfd);

	typedef std::map<int,int>	vfd2fd_ty;
	vfd2fd_ty			vfd2fd;	/* replay fd -> open fd */
	unsigned int			fd_generation;
	bool				ign_close;
};

#endif
