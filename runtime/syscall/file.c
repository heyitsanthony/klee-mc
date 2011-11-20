#include <assert.h>
#include <stdbool.h>
#include <limits.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <klee/klee.h>

#include "syscalls.h"
#include "file.h"
#include "concrete_fd.h"
#include "breadcrumb.h"

#define USE_SYS_FAILURE	1
#define FAILURE_RATE	4

static bool use_concretes = false;

struct fail_counters
{
	unsigned int	fc_read;
	unsigned int	fc_stat;
	unsigned int	fc_write;
} fail_c;


/* 0 => early terminate
 * 1 => normal terminate */
int sc_read_sym(unsigned sys_nr, void* regfile, uint64_t len)
{
	void	*new_regs;

	// This is an error case that we should probably make optional
	// since this causes the state space to explode into really useless
	// code.
	if (len == 0) {
		sc_ret_v(regfile, 0);
		return 1;
	}

	if (len > SSIZE_MAX) {
		/* "result is unspecified"-- -1 for an error */
		sc_ret_v(regfile, -1);
		return 1;
	}

	new_regs = sc_new_regs(regfile);

#ifdef USE_SYS_FAILURE
	if ((++fail_c.fc_read % FAILURE_RATE) == 0 &&
	    (int64_t)GET_RAX(new_regs) == -1) {
	    return 1;
	}
#endif
	klee_assume(GET_RAX(new_regs) == len);

	sc_ret_v(new_regs, len);
	make_sym_by_arg(regfile, 1, len, "readbuf");
	sc_breadcrumb_commit(sys_nr, GET_RAX(new_regs));
	return 0;
}

static void sc_stat_sym(void* regfile)
{
	void* new_regs = sc_new_regs(regfile);
#ifdef USE_SYS_FAILURE
	if (	(++fail_c.fc_stat % FAILURE_RATE) == 0 &&
		(int64_t)GET_RAX(new_regs) == -1)
	{
		return;
	}
#endif
	sc_ret_v(new_regs, 0);
	make_sym_by_arg(regfile, 1, sizeof(struct stat), "statbuf");
}

int str_is_sym(const char* s)
{
	int	i;

	for (i = 0; i < 256; i++) { 
		if (klee_is_symbolic(s[i]))
			return 1;
		if (s[i] == '\0')
			return 0;
	}

	return 1;
}

static void sc_stat(unsigned int sys_nr, void* regfile)
{
	if (sys_nr == SYS_lstat || sys_nr == SYS_stat) {
		const char	*path = (const char*)GET_ARG0(regfile);
		if (!str_is_sym(path)) {
			int	fd;
			fd = fd_open(path);
			if (fd > 0) {
				sc_ret_v(
					regfile,
					fd_stat(fd,
						(struct stat*)GET_ARG1(regfile)));
				fd_close(fd);
				return;
			}
		}
	}

	if (sys_nr == SYS_fstat) {
		int	fd;

		fd = GET_ARG0(regfile);
		if (fd_is_concrete(fd)) {
			sc_ret_v(
				regfile,
				fd_stat(fd, (struct stat*)GET_ARG1(regfile)));
			return;
		}
	}

	klee_warning_once(
		"stat not respecting concretes as it ought to");

	sc_stat_sym(regfile);
}

int file_sc(unsigned int sys_nr, void* regfile)
{
	void	*new_regs;

	switch (sys_nr) {
	case SYS_lseek:
		if (fd_is_concrete(GET_ARG0(regfile))) {
			ssize_t br;
			br = fd_lseek(
				GET_ARG0(regfile),
				(off_t)GET_ARG1(regfile),
				(size_t)GET_ARG2(regfile));
			sc_ret_v(regfile, br);
		} else {
#ifdef USE_SYS_FAILURE
			klee_warning_once("lseek [-1, 4096]");
			sc_ret_range(sc_new_regs(regfile), -1, 4096);
#else
			sc_ret_v(regfile, GET_ARG1(regfile));
#endif
		}
		break;

	case SYS_openat:
	case SYS_open: {
		int	ret_fd;

		new_regs = sc_new_regs(regfile);
		if ((intptr_t)GET_RAX(new_regs) == -1)
			break;

		if (str_is_sym((const char*)GET_ARG0(regfile))) {
			sc_ret_v(new_regs, fd_open_sym());
			break;
		}

		ret_fd = fd_open((const char*)GET_ARG0(regfile));
		if (ret_fd == -1) {
			sc_ret_v(new_regs, fd_open_sym());
			break;
		}

		sc_ret_v(new_regs, ret_fd);
		break;
	}

	case SYS_pread64:
	case SYS_read: 
	{
		uint64_t len = concretize_u64(GET_ARG2(regfile));

		if (use_concretes && fd_is_concrete(GET_ARG0(regfile))) {
			ssize_t	sz;
			sz = fd_read(
				GET_ARG0(regfile),
				(void*)GET_ARG1(regfile),
				len);
			sc_ret_v(regfile, sz);
			break;
		}

		if (sc_read_sym(sys_nr, regfile, len) == 0)
			return 0;
	}
	break;

	case SYS_fstat:
	case SYS_lstat:
	case SYS_stat:
		if (!use_concretes)
			sc_stat_sym(regfile);
		else
			sc_stat(sys_nr, regfile);
		break;

	case SYS_close:
		fd_close(GET_ARG0(regfile));
		sc_ret_v(regfile, 0);
		break;

	case SYS_creat:
		klee_warning_once("phony creat call");
		new_regs = sc_new_regs(regfile);
		if ((int64_t)GET_RAX(new_regs) == -1)
			break;
		klee_assume(GET_RAX(new_regs) > 3 && GET_RAX(new_regs) < 4096);
		break;
	case SYS_readlink:
	{
		if (use_concretes) {
			klee_warning_once(
				"readlink() should work with concretes, "
				"but Anthony is dying.");
		}

		/* keep the string short since we're pure symbolic now */
		/* In the future, use system information to satisfy this */
		uint64_t	addr  = klee_get_value(GET_ARG1(regfile));
		new_regs = sc_new_regs(regfile);
		if (GET_ARG2(regfile) >= 2) {
			sc_ret_range(new_regs, 1, 2);
			make_sym(addr, GET_ARG2(regfile), "readlink");
			// readlink()  does not append a null byte to buf.
			// No need for this:
			// ((char*)addr)[GET_ARG2(new_regs)] = '\0';
			// WOoooo

		} else {
			sc_ret_v(regfile, -1);
		}
	}
	break;
	}

	return 1;
}
