#include <sstream>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>

#include "static/Sugar.h"
#include "FileReconstructor.h"

FileReconstructor::FileReconstructor()
: fd_generation(0)
, max_size(getenv("KMC_MAX_SIZE") != NULL
	? atol(getenv("KMC_MAX_SIZE"))
	: 0)
, ign_close(getenv("KMC_IGN_CLOSE") != NULL)
, file_prefix(getenv("KMC_RECONS_PREFIX") != NULL
	? getenv("KMC_RECONS_PREFIX")
	: "")
{}

FileReconstructor::~FileReconstructor()
{
	foreach (it, vfd2fd.begin(), vfd2fd.end())
		::close(it->second.fd);
}


int FileReconstructor::getGen(int vfd)
{
	vfd2fd_ty::iterator	it;

	it = vfd2fd.find(vfd);
	if (it == vfd2fd.end()) return -1;

	return (it->second).gen;
}

int FileReconstructor::getFD(int vfd)
{
	std::stringstream	ss;
	vfd2fd_ty::iterator	it;
	struct vfd_t		v;

	it = vfd2fd.find(vfd);
	if (it != vfd2fd.end()) return (it->second).fd;

	ss << file_prefix << "recons." << fd_generation;
	v.gen = fd_generation;
	v.vfd = vfd;
	v.fd = open(ss.str().c_str(), O_RDWR|O_CREAT, 0666);
	assert (v.fd >= 0 && "Could not open reconstruction file");

	vfd2fd[vfd] = v;

	fd_generation++;
	return v.fd;
}

void FileReconstructor::read(int vfd, void* buf, size_t count)
{
	char	*tmp_buf;
	int	fd;
	off_t	cur_off;
	uint64_t new_off;

	if (count == ~((size_t)0)) {
		/* error read */
		return;
	}


	if (count == 0xffffffff) {
		/* 32-bit error read */
		return;
	}

	assert (count < 1024*1024*16 && "Huge read??");

	fd = getFD(vfd);

	tmp_buf = new char[count];
	memset(tmp_buf, 0, count);

	cur_off = lseek(fd, 0, SEEK_CUR);
	::read(fd, tmp_buf, count);
	for (unsigned i = 0; i < count; i++)
		tmp_buf[i] |= ((const char*)buf)[i];

	new_off = ::lseek(fd, cur_off, SEEK_SET);
	::write(fd, tmp_buf, count);

	delete [] tmp_buf;

	limitSize(vfd, new_off+count);
}


void FileReconstructor::limitSize(int vfd, uint64_t off)
{
	int	gen;

	if (!max_size) return;
	if (off < max_size) return;

	std::stringstream	ss;
	gen = getGen(vfd);

	ss << file_prefix << "recons." << gen;
	unlink(ss.str().c_str());
}

void FileReconstructor::seek(int vfd, off_t offset, int whence)
{
	uint64_t	off;
	int		fd;

	fd = getFD(vfd);
	off = lseek(getFD(vfd), offset, whence);
	limitSize(vfd, off);
}

void FileReconstructor::close(int vfd)
{
	vfd2fd_ty::iterator	it;

	if (ign_close) {
		std::cerr << "[kmc-recons] Ignoring close\n";
		this->seek(vfd, 0, SEEK_SET);
		return;
	}

	it = vfd2fd.find(vfd);
	if (it == vfd2fd.end())
		return;

	::close(it->second.fd);
	vfd2fd.erase(it);
}
