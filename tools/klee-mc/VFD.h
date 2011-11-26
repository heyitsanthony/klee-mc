#ifndef KLEE_VFD_H
#define KLEE_VFD_H

#include <map>
#include <string>
#include <stdint.h>

typedef uint64_t vfd_t;

namespace klee
{
class VFD
{
public:
	VFD(void) : vfd_counter(0x1000) {}
	virtual ~VFD(void) {}

	vfd_t addPath(const std::string& path);
	int xlateVFD(vfd_t vfd);
	void close(vfd_t vfd);
private:
	typedef std::map<std::string, vfd_t>	path2vfd_t;
	typedef std::map<vfd_t, std::string>	vfd2path_t;
	typedef std::map<vfd_t, int>		vfd2fd_t;

	vfd_t		vfd_counter;
	vfd2fd_t	vfd2fd;
	path2vfd_t	path2vfd;
	vfd2path_t	vfd2path;
};
}

#endif