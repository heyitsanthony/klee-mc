#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include "static/Sugar.h"

#include "VFD.h"

using namespace klee;

int VFD::xlateVFD(vfd_t vfd)
{
	vfd2fd_t::const_iterator 	it;
	vfd2path_t::const_iterator	it2;

	it = vfd2fd.find(vfd);
	if (it != vfd2fd.end())
		return it->second;

	it2 = vfd2path.find(vfd);
	if (it2 == vfd2path.end())
		return -1;

	/* GC'd fd, recreate */
	return openForVFD(vfd, (it2->second).c_str());
}

int VFD::openForVFD(vfd_t vfd, const char* path)
{
	int	base_fd;

	base_fd = open(path, O_RDONLY);
	if (base_fd == -1)
		return -1;

	std::cerr << "[VFD] Opened " << path << ". FD=" << base_fd << '\n';

	vfd2fd[vfd] = base_fd;
	fd2vfd[base_fd].push_back(vfd);

	return base_fd;
}

std::string VFD::getPath(vfd_t vfd) const
{
	vfd2path_t::const_iterator	it;
	
	it = vfd2path.find(vfd);
	if (it == vfd2path.end())
		return "";

	return it->second;
}

vfd_t VFD::addPath(const std::string& path)
{
	path2vfd_t::const_iterator it(path2vfd.find(path));
	vfd_t	vfd;

	if (it != path2vfd.end()) {
		/* already exists! */
		vfd = it->second;
		if (xlateVFD(vfd) == -1)
			vfd = -1;
		return vfd;
	}


	vfd = ++vfd_counter;
	if (openForVFD(vfd, path.c_str()) == -1)
		return -1;

	vfd2path[vfd] = path;
	path2vfd[path] = vfd;

	return vfd;
}

void VFD::close(vfd_t vfd)
{
	vfd2fd_t::iterator	it;

	it = vfd2fd.find(vfd);
	if (it == vfd2fd.end())
		return;

#if 0
	int dead_fd = it->second;
	close(dead_fd);

	std::cerr << "[VFD] Closed fd=" << dead_fd << '\n';

	std::vector<vfd_t>& vfds(fd2vfd[dead_fd]);
	foreach (it2, vfds.begin(), vfds.end()) {
		vfd2fd.erase(*it2);
	}
	fd2vfd.erase(dead_fd);
#endif
}
