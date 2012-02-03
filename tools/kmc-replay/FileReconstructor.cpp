#include <sstream>
#include <assert.h>
#include <string.h>
#include <unistd.h>

#include "static/Sugar.h"
#include "FileReconstructor.h"

FileReconstructor::FileReconstructor()
: fd_generation(0)
{}

FileReconstructor::~FileReconstructor()
{
	foreach (it, vfd2fd.begin(), vfd2fd.end())
		close(it->second);
}

int FileReconstructor::getFD(int vfd)
{
	vfd2fd_ty::iterator	it;

	it = vfd2fd.find(vfd);
	if (it == vfd2fd.end()) {
		std::stringstream	ss;
		int			fd;

		ss << "recons." << fd_generation;
		fd = open(ss.str().c_str(), O_RDWR|O_CREAT, 0666);
		assert (fd >= 0 && "Could not open reconstruction file");

		vfd2fd[vfd] = fd;

		fd_generation++;
		return fd;
	}

	return it->second;
}

void FileReconstructor::read(int vfd, void* buf, size_t count)
{
	char	*tmp_buf;
	int	fd;
	off_t	cur_off;

	assert (count < 1024*1024*16 && "Huge read??");

	fd = getFD(vfd);

	tmp_buf = new char[count];
	memset(tmp_buf, 0, count);

	cur_off = lseek(fd, 0, SEEK_CUR);
	read(fd, tmp_buf, count);
	for (unsigned i = 0; i < count; i++)
		tmp_buf[i] |= ((const char*)buf)[i];

	lseek(fd, cur_off, SEEK_SET);
	write(fd, tmp_buf, cur_off);

	delete [] tmp_buf;
}

void FileReconstructor::seek(int vfd, off_t offset, int whence)
{ lseek(getFD(vfd), offset, whence); }
