// RUN: gcc %s -O0 -I../../../include/  -o %t1
// RUN: klee-mc -pipe-solver -concrete-vfs - ./%t1 2>%t1.err >%t1.out
//
// Should not have any errors.
// RUN: ls klee-last | not grep err

#include "klee/klee.h"
#include <unistd.h>
#include <fcntl.h>
#include <syscall.h>
#include <stdio.h>
#include <sys/mman.h>

int main(int argc, char* argv[])
{
	int	fd;
	char	buf[64];
	ssize_t	br;
	void	*m;
	char	*m_c;

	fd  = open("/etc/group", O_RDONLY);
	if (fd < 0) {
		ksys_report_error(
			__FILE__,
			__LINE__, 
			"Could not open /etc/group. Concretes on?",
			"open.err");
		return -1;
	}

	m = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
	if (m == MAP_FAILED) {
		ksys_report_error(
			__FILE__, __LINE__,
			"Could not mmap /etc/group. What.",
			"mmapfd.err");
	}

	m_c = (char*)m;

	if (m_c[0] != 'r') {
		ksys_report_error(
			__FILE__, __LINE__,
			"Expected root on line 1",
			"data.err");
	}

	/* expect: "root:0"...*/
	if (buf[4] != ':') {
		ksys_report_error(
			__FILE__, __LINE__,
			"Reading bogus groups file. Expected ':' at character 5",
			"data.err");
	}

	/* we did it! */
	munmap(m, 4096);

	close(fd);

	return 0;
}