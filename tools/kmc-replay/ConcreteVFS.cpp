#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include "ConcreteVFS.h"
#include "guest.h"
#include "guestmem.h"
#include "guestabi.h"

bool ConcreteVFS::apply(Guest* g, const SyscallParams& sp, int xlate_nr)
{
	
	switch (xlate_nr) {
	case SYS_mmap: {
		int		fd;
		uint64_t	addr, len, off;
		std::string	path;

		/* (addr, fd, len, off) */
		addr = sp.getArg(0);
		len = sp.getArg(1);
		fd = sp.getArg(4);
		off = sp.getArg(5);
	
		if (fd == -1)
			return false;

		/* don't mark if symbolic length */
		if (len == ~0ULL)
			break;

		assert (0 == 1 && "NEED TO COPY IN");
		break;
	}

	case SYS_readlink: {
		std::string	path;
		char		rl[4096];
		int		rl_n, out_len;
		guest_ptr	guest_addr, path_addr;
		uint64_t	guest_sz;
		
		path_addr = guest_ptr(sp.getArg(0));
		guest_addr = guest_ptr(sp.getArg(1));
		guest_sz = sp.getArg(2);

		if (guest_addr.o == ~0ULL || guest_sz > 4096)
			return false;

		path = g->getMem()->readString(path_addr);
		rl_n = readlink(path.c_str(), rl, 4095);
		if (rl_n == -1)
			return false;

		out_len = guest_sz;
		if (rl_n < out_len) out_len = rl_n;

		g->getMem()->memcpy(guest_addr, rl, out_len);
	//	state.bindLocal(target, MK_CONST(out_len, 64));
		break;
	}

	case SYS_open: {
		std::string	path;
		int		fd, gfd;

		gfd = g->getABI()->getSyscallResult();
		if (gfd == -1)
			return false;

		path = g->getMem()->readString(guest_ptr(sp.getArg(0)));
		fd = open(path.c_str(), O_RDONLY);
		if (fd == -1)
			return false;

		gfd2fd[gfd] = fd;

		std::cerr << "[kmc-io] OPENED '" << path << "'. FD="
			<< (long)fd << '\n';
		break;
	}
	case SYS_close: {
		int	fd;

		fd = sp.getArg(0);
		if (!gfd2fd.count(fd))
			break;

		close(gfd2fd[fd]);
		gfd2fd.erase(fd);
		break;
	}

	case SYS_read: {
		int	fd, gfd;
		size_t	count, br;
		char	*tmp_buf;

		gfd = sp.getArg(0);
		if (gfd2fd.count(gfd) == 0)
			return false;

		fd = gfd2fd[gfd];
		count = sp.getArg(2);

		tmp_buf = new char[count];
		br = read(fd, tmp_buf, count);
		g->getMem()->memcpy(guest_ptr(sp.getArg(1)), tmp_buf, count);
		delete [] tmp_buf;

		std::cerr << "[kmc-io] Read fd=" << gfd << ". br=" << br << "\n";

		break;
	}

	case SYS_pread64: {
		int		fd, gfd;
		ssize_t		ret;
		guest_ptr	buf_base;
		size_t		count;
		off_t		offset;
		char		*tmp_buf;

		gfd = sp.getArg(0);
		buf_base = guest_ptr(sp.getArg(1));
		count = sp.getArg(2);
		offset = sp.getArg(3);

		if (	gfd == -1 || buf_base == ~0ULL ||
			count == ~0ULL || offset == -1 || !gfd2fd.count(gfd))
		{
			return false;
		}

		fd = gfd2fd[gfd];
		tmp_buf = new char[count];
		ret = pread64(fd, tmp_buf, count, offset);
		g->getMem()->memcpy(buf_base, tmp_buf, count);
		delete [] tmp_buf;

		break;
	}

	case SYS_fstat: {
		struct stat	s;
		int		fd, gfd;
		int		rc;

		gfd = sp.getArg(0);
		if (gfd == -1 || !gfd2fd.count(gfd)) return false;

		fd = gfd2fd[gfd];

		rc = fstat(fd, &s);
		if (rc == -1) return false;

		g->getMem()->memcpy(guest_ptr(sp.getArg(1)), &s, sizeof(s));

		std::cerr << "[kmc-io] fstat fd="  << fd << "\n";
		break;

	default:
		return false;
	}}

	return true;
}
