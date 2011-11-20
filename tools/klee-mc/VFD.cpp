#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include "VFD.h"

using namespace klee;

int VFD::xlateVFD(vfd_t vfd)
{
	vfd2fd_t::const_iterator 	it;
	vfd2path_t::const_iterator	it2;
	int				base_fd;

	it = vfd2fd.find(vfd);
	if (it != vfd2fd.end())
		return it->second;

	it2 = vfd2path.find(vfd);
	if (it2 == vfd2path.end())
		return -1;

	/* GC'd fd, recreate */
	base_fd = open((it2->second).c_str(), O_RDONLY);
	vfd2fd[vfd] = base_fd;
	return base_fd;
}

vfd_t VFD::addPath(const std::string& path)
{
	path2vfd_t::const_iterator it(path2vfd.find(path));
	vfd_t	vfd;
	int	base_fd;

	if (it != path2vfd.end()) {
		/* already exists! */
		vfd = it->second;
		if (xlateVFD(vfd) == -1)
			vfd = -1;

		return vfd;
	}


	if (it != path2vfd.end()) {
		/* already exist! */
		vfd = it->second;
		if (xlateVFD(vfd) == -1)
			vfd = -1;
		return vfd;
	}

	vfd = vfd_counter++;
	base_fd = open(path.c_str(), O_RDONLY);
	vfd2fd[vfd] = base_fd;
	vfd2path[vfd] = path;

	return vfd;
}

void VFD::close(vfd_t vfd)
{
	vfd2fd_t::iterator	it;

	it = vfd2fd.find(vfd);
	if (it == vfd2fd.end())
		return;

	close(it->second);
	vfd2fd.erase(it);
}
