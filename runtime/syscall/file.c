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

bool concrete_vfs = false;
bool deny_sys_files = false;

struct fail_counters
{
	unsigned int	fc_read;
	unsigned int	fc_stat;
	unsigned int	fc_write;
} fail_c;

/* XXX, yeah these are stupid. */
#warning fix strlen so we dont reimplement
static int strlen(const char* x)
{
	int	k = 0;
	while (x[k]) k++;
	return k;
}

static int memcmp(const void* ugh, const void* ugh2, unsigned int len)
{
	const char	*ugh_c = ugh, *ugh2_c = ugh2;
	unsigned int	k;
	k = 0;
	while (k < len) {
		int	r;
		r = ugh_c[k] - ugh2_c[k];
		if (r != 0)
			return r;
		k++;
	}
	return 0;
}

static int str_contains(const char* needle, const char* haystack)
{
	int	len_hs, len_n;
	int	k;

	len_hs = strlen(haystack);
	len_n = strlen(needle);

	for (k = 0; k < len_hs - len_n; k++) {
		if (memcmp(&haystack[k], needle, len_n) == 0)
			return 1;
	}
	return 0;
}
/* XXX GET RID OF THESE */

/* 0 => early terminate
 * 1 => normal terminate */
int sc_read_sym(
	unsigned pure_sysnr,
	unsigned sys_nr, void* regfile, uint64_t len)
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
	    GET_SYSRET_S(new_regs) == -1) {
	    return 1;
	}
#endif
	klee_assume(GET_SYSRET(new_regs) == len);

	sc_ret_v(new_regs, len);
	make_sym_by_arg(regfile, 1, len, "readbuf");
	sc_breadcrumb_commit(pure_sysnr, sys_nr, GET_SYSRET(new_regs));
	return 0;
}

static void sc_stat_sym(void* regfile)
{
	void* new_regs = sc_new_regs(regfile);
#ifdef USE_SYS_FAILURE
	if (	(++fail_c.fc_stat % FAILURE_RATE) == 0 &&
		GET_SYSRET_S(new_regs) == -1)
	{
		return;
	}
#endif
	sc_ret_v(new_regs, 0);

/* XXX FIXME XXX
 * what happens if I'm 32-bit and fstat64? Shouldn't I be using the
 * 64 bit struct instead? */
#if (GUEST_ARCH_ARM || GUEST_ARCH_X86)
	/* 32-bit statbuf */
	make_sym_by_arg(regfile, 1, 88, "statbuf");
#else
	make_sym_by_arg(regfile, 1, sizeof(struct stat), "statbuf");
#endif
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
		const char	*path;

		path = (const char*)(GET_ARG0_PTR(regfile));
		if (!str_is_sym(path)) {
			int		fd;

			fd = fd_open(path);
			if (fd > 0) {
				struct stat	*s;
				s = (struct stat*)(GET_ARG1_PTR(regfile));
				sc_ret_v(regfile, fd_stat(fd, s));
				fd_close(fd);
				return;
			}
		}
	}

	if (sys_nr == SYS_fstat) {
		int	fd;

		fd = GET_ARG0(regfile);
		if (fd_is_concrete(fd)) {
			struct stat	*s;

			s = (struct stat*)(GET_ARG1_PTR(regfile));
			sc_ret_v(regfile, fd_stat(fd, s));

			return;
		}
	}

	klee_warning_once(
		"stat not respecting concretes as it ought to");

	sc_stat_sym(regfile);
}

static void sc_open(const char* path, void* regfile)
{
	void		*new_regs;
	int		ret_fd;

	if (path == NULL) {
		sc_ret_v(regfile, -1);
		return;
	}

	if (!str_is_sym(path)) {
		if (	deny_sys_files &&
			(path[0] == '\0' ||
			(path[0] == '/' &&
			(path[1] == 'u' || path[1] == 'l')) ||
			str_contains(".so", path)))
		{
			sc_ret_v(regfile, -1);
			return;
		}

		if (concrete_vfs) {
			ret_fd = fd_open(path);
			if (	ret_fd != -1 ||
				(path[0] == '/' &&
				(path[1] == 'l' || path[1] == 'u')))
			{
				sc_ret_v(regfile, ret_fd);
				return;
			}
		}
	}

	new_regs = sc_new_regs(regfile);
	if (GET_SYSRET_S(new_regs) == -1) {
		sc_ret_v(new_regs, -1);
		return;
	}

	ret_fd = fd_open_sym();
	klee_assume(GET_SYSRET_S(new_regs) == ret_fd);
	sc_ret_v(new_regs, ret_fd);
}

int file_sc(unsigned int pure_sysnr, unsigned int sys_nr, void* regfile)
{
	void	*new_regs;

	switch (sys_nr) {
	case SYS_lseek: {
		int fd = GET_ARG0(regfile);
		if (fd_is_concrete(fd)) {
			ssize_t br;
			br = fd_lseek(
				fd,
				(off_t)GET_ARG1(regfile),
				(size_t)GET_ARG2(regfile));
			sc_ret_v(regfile, br);
		} else {
#ifdef USE_SYS_FAILURE
			ssize_t br = GET_ARG1(regfile);
			klee_warning_once("lseek [-1, 4096]");
			if (br > 0) {
				// br = concretize_u64(br);
				// new_regs = sc_new_regs(regfile);
				/* return unboundd range */
				sc_ret_or(sc_new_regs(regfile), -1, br);
			} else
				sc_ret_v(regfile, -1);
#else
			sc_ret_v(regfile, GET_ARG1(regfile));
#endif
		}
		break;
	}
	case SYS_openat:
		klee_warning_once("openat is kind of bogus");
		sc_open((const char*)GET_ARG1_PTR(regfile), regfile);
		break;

	case SYS_open:
		sc_open((const char*)GET_ARG0_PTR(regfile), regfile);
		break;

	case SYS_pread64:
	case SYS_read:
	{
		int		fd = GET_ARG0(regfile);
		uint64_t	len = concretize_u64(GET_ARG2(regfile));

		if (concrete_vfs && fd_is_concrete(fd)) {
			ssize_t	sz;
			sz = fd_read(
				GET_ARG0(regfile),
				(void*)(GET_ARG1_PTR(regfile)),
				len);
			sc_ret_v(regfile, sz);
			break;
		}

		if (sc_read_sym(pure_sysnr, sys_nr, regfile, len) == 0)
			return 0;
	}
	break;

	case SYS_fstat:
	case SYS_lstat:
	case SYS_stat:
		if (!concrete_vfs)
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
		if (GET_SYSRET_S(new_regs) == -1)
			break;
		klee_assume(GET_SYSRET(new_regs) > 3 && GET_SYSRET(new_regs) < 4096);
		break;
	case SYS_readlink:
	{
		if (concrete_vfs) {
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
